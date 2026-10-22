#include "lcero/ir/optimized_backend.hpp"

#include "lcero/ir/register_allocation.hpp"

#include <set>
#include <sstream>
#include <type_traits>

namespace lcero::ir {
namespace {

void report_backend_error(
    common::DiagnosticBag& diagnostics,
    const std::filesystem::path& path,
    const std::string& message) {
    diagnostics.report(
        common::DiagnosticCategory::CodeGeneration,
        common::DiagnosticSeverity::Error,
        message,
        common::SourceLocation{path});
}

std::string label_name(const LabelId label) {
    return "L" + std::to_string(label.id);
}

const char* register_name(const std::size_t color) {
    return color == 0 ? "R2" : "R3";
}

bool canonical_integer(const std::string& value) {
    if (value == "0") {
        return true;
    }
    std::size_t index = value.empty() || value.front() != '-' ? 0 : 1;
    if (index >= value.size() || value[index] == '0') {
        return false;
    }
    for (; index < value.size(); ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    return true;
}

} // namespace

BackendResult OptimizedBackend::emit(
    const Program& program,
    const std::filesystem::path& output_path,
    common::DiagnosticBag& diagnostics) const {
    bool has_halt = false;
    for (const auto& record : program.instructions) {
        has_halt = has_halt ||
            std::holds_alternative<HaltInstruction>(record.instruction);
    }
    if (!has_halt) {
        report_backend_error(diagnostics, output_path,
                             "La RI optimizada no contiene HALT.");
        return {};
    }

    const ControlFlowGraphBuilder cfg_builder;
    auto cfg_result = cfg_builder.build(program, output_path, diagnostics);
    if (!cfg_result.succeeded) {
        return {};
    }

    const LivenessAnalyzer liveness_analyzer;
    auto liveness = liveness_analyzer.analyze(
        program, std::move(cfg_result.graph), output_path, diagnostics);
    if (!liveness.succeeded) {
        return {};
    }

    const InterferenceGraphBuilder interference_builder;
    const auto interference = interference_builder.build(program, liveness);
    const ChaitinAllocator allocator;
    const auto allocation = allocator.allocate(
        interference, program.variables.size(), 2);
    if (!allocation.succeeded) {
        report_backend_error(diagnostics, output_path,
                             "Chaitin no pudo producir una asignacion fisica valida.");
        return {};
    }

    for (std::size_t index = 0; index < program.variables.size(); ++index) {
        if (program.variables[index].id.id != index ||
            program.variables[index].address != index) {
            report_backend_error(diagnostics, output_path,
                                 "La tabla de variables de la RI es inconsistente.");
            return {};
        }
    }

    const auto memory_slots =
        program.variables.size() + allocation.spilled_temporary_count;
    std::ostringstream output;
    output << "LCERO_IR 1\nMEM " << memory_slots << '\n';
    std::size_t physical_count = 0;
    std::size_t load_count = 0;
    std::size_t store_count = 0;

    auto colored_register = [&](const VirtualTemporary temporary)
        -> std::optional<std::string> {
        if (temporary.id >= allocation.colors.size()) {
            return std::nullopt;
        }
        if (!allocation.colors[temporary.id].has_value()) {
            return std::nullopt;
        }
        return register_name(allocation.colors[temporary.id].value());
    };
    auto spill_slot = [&](const VirtualTemporary temporary)
        -> std::optional<std::size_t> {
        if (temporary.id >= allocation.spill_slots.size()) {
            return std::nullopt;
        }
        return allocation.spill_slots[temporary.id];
    };
    auto materialize = [&](const VirtualTemporary temporary, const char* scratch)
        -> std::optional<std::string> {
        const auto reg = colored_register(temporary);
        if (reg.has_value()) {
            return reg;
        }
        const auto slot = spill_slot(temporary);
        if (!slot.has_value()) {
            return std::nullopt;
        }
        output << "LOAD " << scratch << ", " << slot.value() << '\n';
        ++physical_count;
        ++load_count;
        return std::string(scratch);
    };
    auto destination = [&](const VirtualTemporary temporary)
        -> std::optional<std::string> {
        const auto reg = colored_register(temporary);
        if (reg.has_value()) {
            return reg;
        }
        return spill_slot(temporary).has_value()
            ? std::optional<std::string>{"R0"}
            : std::nullopt;
    };
    auto store_spill = [&](const VirtualTemporary temporary, const std::string& reg) {
        const auto slot = spill_slot(temporary);
        if (slot.has_value()) {
            output << "STORE " << slot.value() << ", " << reg << '\n';
            ++physical_count;
            ++store_count;
        }
    };

    bool valid = true;
    for (const auto& record : program.instructions) {
        std::visit([&](const auto& instruction) {
            using T = std::decay_t<decltype(instruction)>;
            if constexpr (std::is_same_v<T, ConstantInstruction>) {
                const auto target = destination(instruction.destination);
                if (!target.has_value() ||
                    !canonical_integer(instruction.decimal_value)) {
                    valid = false;
                    return;
                }
                output << "CONST " << target.value() << ", "
                       << instruction.decimal_value << '\n';
                ++physical_count;
                store_spill(instruction.destination, target.value());
            } else if constexpr (std::is_same_v<T, LoadVariableInstruction>) {
                const auto target = destination(instruction.destination);
                if (!target.has_value() ||
                    instruction.variable.id >= program.variables.size()) {
                    valid = false;
                    return;
                }
                output << "LOAD " << target.value() << ", "
                       << program.variables[instruction.variable.id].address << '\n';
                ++physical_count;
                ++load_count;
                store_spill(instruction.destination, target.value());
            } else if constexpr (std::is_same_v<T, StoreVariableInstruction>) {
                const auto source = materialize(instruction.source, "R0");
                if (!source.has_value() ||
                    instruction.variable.id >= program.variables.size()) {
                    valid = false;
                    return;
                }
                output << "STORE "
                       << program.variables[instruction.variable.id].address
                       << ", " << source.value() << '\n';
                ++physical_count;
                ++store_count;
            } else if constexpr (std::is_same_v<T, BinaryInstruction> ||
                                 std::is_same_v<T, ComparisonInstruction>) {
                const auto left = materialize(instruction.left, "R0");
                const auto right = instruction.left.id == instruction.right.id
                    ? left
                    : materialize(instruction.right, "R1");
                const auto target = destination(instruction.destination);
                const bool operation_valid =
                    (std::is_same_v<T, BinaryInstruction> &&
                     is_arithmetic_opcode(instruction.operation)) ||
                    (std::is_same_v<T, ComparisonInstruction> &&
                     is_comparison_opcode(instruction.operation));
                if (!left.has_value() || !right.has_value() ||
                    !target.has_value() || !operation_valid) {
                    valid = false;
                    return;
                }
                output << opcode_name(instruction.operation) << ' '
                       << target.value() << ", " << left.value() << ", "
                       << right.value() << '\n';
                ++physical_count;
                store_spill(instruction.destination, target.value());
            } else if constexpr (std::is_same_v<T, JumpInstruction>) {
                output << "JMP " << label_name(instruction.target) << '\n';
                ++physical_count;
            } else if constexpr (std::is_same_v<T, JumpIfZeroInstruction>) {
                const auto condition = materialize(instruction.condition, "R0");
                if (!condition.has_value()) {
                    valid = false;
                    return;
                }
                output << "JZ " << condition.value() << ", "
                       << label_name(instruction.target) << '\n';
                ++physical_count;
            } else if constexpr (std::is_same_v<T, LabelInstruction>) {
                output << "LABEL " << label_name(instruction.label) << '\n';
            } else if constexpr (std::is_same_v<T, PrintInstruction>) {
                const auto source = materialize(instruction.source, "R0");
                if (!source.has_value()) {
                    valid = false;
                    return;
                }
                output << "PRINT " << source.value() << '\n';
                ++physical_count;
            } else {
                output << "HALT\n";
                ++physical_count;
            }
        }, record.instruction);
        if (!valid) {
            report_backend_error(diagnostics, output_path,
                                 "La RI contiene operandos invalidos para el backend optimizado.");
            return {};
        }
    }

    return BackendResult{
        true,
        output.str(),
        memory_slots,
        physical_count,
        load_count,
        store_count,
        liveness.graph.blocks.size(),
        liveness.graph.edge_count,
        interference.adjacency.size(),
        interference.edge_count,
        allocation.registers_used,
        allocation.spilled_temporary_count,
        liveness.fixed_point_iterations
    };
}

} // namespace lcero::ir
