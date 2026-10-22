#include "lcero/ir/reference_backend.hpp"

#include <limits>
#include <set>
#include <sstream>
#include <type_traits>

namespace lcero::ir {
namespace {

void report_generation_error(
    common::DiagnosticBag& diagnostics,
    const std::filesystem::path& path,
    const std::string& message) {
    diagnostics.report(
        common::DiagnosticCategory::CodeGeneration,
        common::DiagnosticSeverity::Error,
        message,
        common::SourceLocation{path});
}

bool is_canonical_integer(const std::string& text) {
    if (text == "0") {
        return true;
    }
    std::size_t index = 0;
    if (!text.empty() && text.front() == '-') {
        index = 1;
    }
    if (index >= text.size() || text[index] == '0') {
        return false;
    }
    for (; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
    }
    return true;
}

std::string label_name(const LabelId label) {
    return "L" + std::to_string(label.id);
}

} // namespace

BackendResult ReferenceBackend::emit(
    const Program& program,
    const std::filesystem::path& output_path,
    common::DiagnosticBag& diagnostics) const {
    if (program.temporary_count >
        std::numeric_limits<std::size_t>::max() - program.variables.size()) {
        report_generation_error(
            diagnostics, output_path, "La cantidad total de slots excede size_t.");
        return {};
    }

    for (std::size_t index = 0; index < program.variables.size(); ++index) {
        const auto& variable = program.variables[index];
        if (variable.id.id != index || variable.address != index) {
            report_generation_error(
                diagnostics,
                output_path,
                "La tabla de direcciones de variables de la RI es inconsistente.");
            return {};
        }
    }

    std::set<std::size_t> defined_labels;
    std::set<std::size_t> referenced_labels;
    bool has_halt = false;

    auto valid_temporary = [&](const VirtualTemporary value) {
        return value.id < program.temporary_count;
    };
    auto valid_variable = [&](const VariableId value) {
        return value.id < program.variables.size();
    };

    for (const auto& record : program.instructions) {
        const bool valid = std::visit([&](const auto& instruction) {
            using T = std::decay_t<decltype(instruction)>;
            if constexpr (std::is_same_v<T, ConstantInstruction>) {
                return valid_temporary(instruction.destination) &&
                    is_canonical_integer(instruction.decimal_value);
            } else if constexpr (std::is_same_v<T, LoadVariableInstruction>) {
                return valid_temporary(instruction.destination) &&
                    valid_variable(instruction.variable);
            } else if constexpr (std::is_same_v<T, StoreVariableInstruction>) {
                return valid_variable(instruction.variable) &&
                    valid_temporary(instruction.source);
            } else if constexpr (std::is_same_v<T, BinaryInstruction>) {
                return is_arithmetic_opcode(instruction.operation) &&
                    valid_temporary(instruction.destination) &&
                    valid_temporary(instruction.left) &&
                    valid_temporary(instruction.right);
            } else if constexpr (std::is_same_v<T, ComparisonInstruction>) {
                return is_comparison_opcode(instruction.operation) &&
                    valid_temporary(instruction.destination) &&
                    valid_temporary(instruction.left) &&
                    valid_temporary(instruction.right);
            } else if constexpr (std::is_same_v<T, JumpInstruction>) {
                referenced_labels.insert(instruction.target.id);
                return instruction.target.id < program.label_count;
            } else if constexpr (std::is_same_v<T, JumpIfZeroInstruction>) {
                referenced_labels.insert(instruction.target.id);
                return valid_temporary(instruction.condition) &&
                    instruction.target.id < program.label_count;
            } else if constexpr (std::is_same_v<T, LabelInstruction>) {
                return instruction.label.id < program.label_count &&
                    defined_labels.insert(instruction.label.id).second;
            } else if constexpr (std::is_same_v<T, PrintInstruction>) {
                return valid_temporary(instruction.source);
            } else {
                has_halt = true;
                return true;
            }
        }, record.instruction);

        if (!valid) {
            report_generation_error(
                diagnostics,
                output_path,
                "La RI interna contiene operandos u operaciones invalidos.");
            return {};
        }
    }

    for (const auto label : referenced_labels) {
        if (defined_labels.count(label) == 0) {
            report_generation_error(
                diagnostics,
                output_path,
                "La RI interna referencia una etiqueta no definida: " +
                    label_name(LabelId{label}) + ".");
            return {};
        }
    }
    if (!has_halt) {
        report_generation_error(
            diagnostics, output_path, "La RI interna no contiene HALT.");
        return {};
    }

    const std::size_t memory_slots =
        program.variables.size() + program.temporary_count;
    auto temporary_slot = [&](const VirtualTemporary temporary) {
        return program.variables.size() + temporary.id;
    };

    std::ostringstream output;
    output << "LCERO_IR 1\nMEM " << memory_slots << '\n';
    std::size_t physical_instruction_count = 0;
    std::size_t load_count = 0;
    std::size_t store_count = 0;

    // Cada temporal vive en memoria. Las cargas a R0/R1 y el resultado en R2
    // implementan una instruccion de tres direcciones sin inventar registros.
    for (const auto& record : program.instructions) {
        std::visit([&](const auto& instruction) {
            using T = std::decay_t<decltype(instruction)>;
            if constexpr (std::is_same_v<T, ConstantInstruction>) {
                output << "CONST R0, " << instruction.decimal_value << '\n'
                       << "STORE " << temporary_slot(instruction.destination)
                       << ", R0\n";
                physical_instruction_count += 2;
                ++store_count;
            } else if constexpr (std::is_same_v<T, LoadVariableInstruction>) {
                output << "LOAD R0, "
                       << program.variables[instruction.variable.id].address << '\n'
                       << "STORE " << temporary_slot(instruction.destination)
                       << ", R0\n";
                physical_instruction_count += 2;
                ++load_count;
                ++store_count;
            } else if constexpr (std::is_same_v<T, StoreVariableInstruction>) {
                output << "LOAD R0, " << temporary_slot(instruction.source) << '\n'
                       << "STORE "
                       << program.variables[instruction.variable.id].address
                       << ", R0\n";
                physical_instruction_count += 2;
                ++load_count;
                ++store_count;
            } else if constexpr (std::is_same_v<T, BinaryInstruction> ||
                                 std::is_same_v<T, ComparisonInstruction>) {
                output << "LOAD R0, " << temporary_slot(instruction.left) << '\n'
                       << "LOAD R1, " << temporary_slot(instruction.right) << '\n'
                       << opcode_name(instruction.operation) << " R2, R0, R1\n"
                       << "STORE " << temporary_slot(instruction.destination)
                       << ", R2\n";
                physical_instruction_count += 4;
                load_count += 2;
                ++store_count;
            } else if constexpr (std::is_same_v<T, JumpInstruction>) {
                output << "JMP " << label_name(instruction.target) << '\n';
                ++physical_instruction_count;
            } else if constexpr (std::is_same_v<T, JumpIfZeroInstruction>) {
                output << "LOAD R0, " << temporary_slot(instruction.condition) << '\n'
                       << "JZ R0, " << label_name(instruction.target) << '\n';
                physical_instruction_count += 2;
                ++load_count;
            } else if constexpr (std::is_same_v<T, LabelInstruction>) {
                output << "LABEL " << label_name(instruction.label) << '\n';
            } else if constexpr (std::is_same_v<T, PrintInstruction>) {
                output << "LOAD R0, " << temporary_slot(instruction.source) << '\n'
                       << "PRINT R0\n";
                physical_instruction_count += 2;
                ++load_count;
            } else {
                output << "HALT\n";
                ++physical_instruction_count;
            }
        }, record.instruction);
    }

    return BackendResult{
        true,
        output.str(),
        memory_slots,
        physical_instruction_count,
        load_count,
        store_count
    };
}

} // namespace lcero::ir
