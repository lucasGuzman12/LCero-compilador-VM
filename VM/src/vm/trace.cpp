#include "lcero/vm/trace.hpp"

#include <array>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace lcero::vm {
namespace {

std::string register_name(const RegisterId id) {
    return "R" + std::to_string(static_cast<unsigned int>(id));
}

std::string operand_text(const Operand& operand) {
    if (const auto* reg = std::get_if<RegisterId>(&operand)) {
        return register_name(*reg);
    }
    if (const auto* slot = std::get_if<MemorySlot>(&operand)) {
        return "M[" + to_decimal_string(slot->index) + "]";
    }
    if (const auto* immediate = std::get_if<Immediate>(&operand)) {
        return to_decimal_string(immediate->value);
    }
    const auto* target = std::get_if<JumpTarget>(&operand);
    return target == nullptr
               ? "OPERANDO_INVALIDO"
               : "pc=" + std::to_string(target->instruction_index);
}

std::string operands_text(const Instruction& instruction) {
    std::string text = "[";
    for (std::size_t index = 0; index < instruction.operands.size(); ++index) {
        if (index != 0U) {
            text += ',';
        }
        text += operand_text(instruction.operands[index]);
    }
    text += ']';
    return text;
}

std::string registers_text(const RegisterFile& registers) {
    std::string text = "[";
    for (std::size_t index = 0; index < registers.size(); ++index) {
        if (index != 0U) {
            text += ';';
        }
        text += "R" + std::to_string(index) + '=';
        text += registers[index].has_value()
                    ? to_decimal_string(*registers[index])
                    : "UNSET";
    }
    text += ']';
    return text;
}

std::string terminal_category(const ExecutionState& state) {
    if (state.status() == MachineStatus::halt) {
        return "TERMINACION";
    }
    if (state.status() == MachineStatus::limit) {
        return "PRESUPUESTO";
    }
    if (state.terminal_diagnostic().has_value() &&
        state.terminal_diagnostic()->category == DiagnosticCategory::resource) {
        return "RECURSOS";
    }
    return "EJECUCION";
}

bool writes_register(const Opcode opcode) noexcept {
    switch (opcode) {
        case Opcode::constant:
        case Opcode::load:
        case Opcode::move:
        case Opcode::add:
        case Opcode::subtract:
        case Opcode::multiply:
        case Opcode::divide:
        case Opcode::compare_equal:
        case Opcode::compare_not_equal:
        case Opcode::compare_less:
        case Opcode::compare_less_equal:
        case Opcode::compare_greater:
        case Opcode::compare_greater_equal:
            return true;
        case Opcode::store:
        case Opcode::jump:
        case Opcode::jump_if_zero:
        case Opcode::print:
        case Opcode::halt:
            return false;
    }
    return false;
}

std::string state_diff(const ExecutionState& state,
                       const Instruction& instruction,
                       const std::size_t previous_pc,
                       const std::uint64_t previous_steps) {
    // Una instruccion dinamicamente fallida no completa una transicion y no
    // incrementa k; por lo tanto no se atribuye un cambio semantico ficticio.
    const bool completed = state.executed_steps() == previous_steps + 1U;
    if (!completed) {
        return "none";
    }

    if (writes_register(instruction.opcode)) {
        const auto* destination =
            std::get_if<RegisterId>(&instruction.operands.at(0U));
        if (destination != nullptr) {
            const auto& value =
                state.registers().at(static_cast<std::size_t>(*destination));
            if (value.has_value()) {
                return register_name(*destination) + '=' +
                       to_decimal_string(*value);
            }
        }
    }

    if (instruction.opcode == Opcode::store) {
        const auto* slot =
            std::get_if<MemorySlot>(&instruction.operands.at(0U));
        if (slot != nullptr) {
            const auto found = state.initialized_memory().find(slot->index);
            if (found != state.initialized_memory().end()) {
                return "M[" + to_decimal_string(slot->index) + "]=" +
                       to_decimal_string(found->second);
            }
        }
    }

    if (instruction.opcode == Opcode::jump ||
        instruction.opcode == Opcode::jump_if_zero) {
        return "pc:" + std::to_string(previous_pc) + "->" +
               std::to_string(state.program_counter());
    }

    if (instruction.opcode == Opcode::print &&
        !state.output_values().empty()) {
        return "output_append=" +
               to_decimal_string(state.output_values().back()) + "\\n";
    }

    if (instruction.opcode == Opcode::halt) {
        return "status:RUNNING->HALT";
    }
    return "none";
}

}  // namespace

Result<TraceWriter> TraceWriter::open(const std::filesystem::path& path) {
    try {
        // binary evita que Windows traduzca los LF del formato de diagnostico.
        std::ofstream output(path,
                             std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output.is_open() || !output.good()) {
            return Result<TraceWriter>::failure(
                {DiagnosticCategory::resource,
                 DiagnosticCode::trace_open_failed,
                 "no se pudo abrir la ruta de traza"});
        }
        return Result<TraceWriter>::success(TraceWriter(std::move(output)));
    } catch (const std::exception&) {
        return Result<TraceWriter>::failure(
            {DiagnosticCategory::resource,
             DiagnosticCode::trace_open_failed,
             "fallo fisico al abrir la ruta de traza"});
    }
}

TraceWriter::TraceWriter(std::ofstream output)
    : output_(std::move(output)) {}

void TraceWriter::record_failure(const DiagnosticCode code,
                                 std::string message) {
    if (!diagnostic_.has_value()) {
        diagnostic_ = Diagnostic{DiagnosticCategory::resource, code,
                                 std::move(message)};
    }
}

void TraceWriter::write_text(const std::string& text) {
    if (diagnostic_.has_value()) {
        return;
    }
    try {
        output_.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output_.good()) {
            record_failure(DiagnosticCode::trace_write_failed,
                           "no se pudo escribir el log de traza");
        }
    } catch (const std::exception&) {
        record_failure(DiagnosticCode::trace_write_failed,
                       "fallo fisico al escribir el log de traza");
    }
}

void TraceWriter::write_header(const ValidatedIr& validated_ir) {
    try {
        std::ostringstream text;
        text << "LCERO_TRACE 1\n"
             << "FORMAT mode=DIFF encoding=UTF-8 line_end=LF\n"
             << "PROGRAM instructions=" << validated_ir.program.size()
             << " memory_bound="
             << to_decimal_string(validated_ir.memory_size.value) << '\n';
        for (const auto& label : validated_ir.labels) {
            text << "LABEL name=" << label.first << " pc=" << label.second
                 << '\n';
        }
        text << "INITIAL k=0 pc=0 status=RUNNING "
             << "registers=[R0=UNSET;R1=UNSET;R2=UNSET;R3=UNSET] "
             << "memory={} output_count=0\n";
        write_text(text.str());
    } catch (const std::exception&) {
        record_failure(DiagnosticCode::trace_write_failed,
                       "no se pudo serializar el encabezado de traza");
    }
}

void TraceWriter::write_before(const ExecutionState& state,
                               const Instruction& instruction) {
    try {
        std::ostringstream text;
        text << "BEFORE k=" << state.executed_steps()
             << " pc=" << state.program_counter()
             << " exec_index=" << state.program_counter()
             << " source_line=" << instruction.source_line
             << " opcode=" << opcode_name(instruction.opcode)
             << " operands=" << operands_text(instruction)
             << " registers=" << registers_text(state.registers()) << '\n';
        write_text(text.str());
    } catch (const std::exception&) {
        record_failure(DiagnosticCode::trace_write_failed,
                       "no se pudo serializar BEFORE en la traza");
    }
}

void TraceWriter::write_after(const ExecutionState& state,
                              const Instruction& instruction,
                              const std::size_t previous_pc,
                              const std::uint64_t previous_steps) {
    try {
        std::ostringstream text;
        text << "AFTER k=" << state.executed_steps()
             << " pc=" << state.program_counter()
             << " exec_index=" << previous_pc
             << " source_line=" << instruction.source_line
             << " opcode=" << opcode_name(instruction.opcode)
             << " status=" << machine_status_name(state.status())
             << " diff=["
             << state_diff(state, instruction, previous_pc, previous_steps)
             << ']';
        if (state.terminal_diagnostic().has_value()) {
            text << " diagnostic="
                 << diagnostic_code_name(
                        state.terminal_diagnostic()->code);
        }
        text << '\n';
        write_text(text.str());
    } catch (const std::exception&) {
        record_failure(DiagnosticCode::trace_write_failed,
                       "no se pudo serializar AFTER en la traza");
    }
}

void TraceWriter::finalize(const ExecutionState& state) {
    if (finalized_) {
        return;
    }
    finalized_ = true;
    try {
        std::ostringstream text;
        text << "TERMINAL status=" << machine_status_name(state.status())
             << " k=" << state.executed_steps()
             << " pc=" << state.program_counter()
             << " category=" << terminal_category(state);
        if (state.status() == MachineStatus::limit) {
            text << " reason=STEP_BUDGET_EXHAUSTED"
                 << " note=LIMIT_IS_NOT_NONTERMINATION_PROOF";
        }
        if (state.terminal_diagnostic().has_value()) {
            text << " diagnostic="
                 << diagnostic_code_name(state.terminal_diagnostic()->code);
        }
        text << '\n';
        write_text(text.str());
        output_.flush();
        if (!output_.good()) {
            record_failure(DiagnosticCode::trace_write_failed,
                           "no se pudo finalizar el log de traza");
        }
    } catch (const std::exception&) {
        record_failure(DiagnosticCode::trace_write_failed,
                       "fallo fisico al finalizar el log de traza");
    }
}

bool TraceWriter::ok() const noexcept {
    return !diagnostic_.has_value();
}

const std::optional<Diagnostic>& TraceWriter::diagnostic() const noexcept {
    return diagnostic_;
}

}  // namespace lcero::vm
