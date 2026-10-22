#include "lcero/vm/interpreter.hpp"
#include "lcero/vm/trace.hpp"

#include <exception>
#include <new>
#include <string>
#include <utility>

namespace lcero::vm {
namespace {

Diagnostic internal_diagnostic(const std::size_t source_line,
                               std::string message) {
    return {DiagnosticCategory::internal,
            DiagnosticCode::invalid_instruction_state,
            std::move(message), source_line};
}

template <typename OperandType>
const OperandType* operand_as(const Instruction& instruction,
                              const std::size_t position) {
    if (position >= instruction.operands.size()) {
        return nullptr;
    }
    return std::get_if<OperandType>(&instruction.operands[position]);
}

Result<ExecutionState> fail_internal(ExecutionState state,
                                     const Instruction& instruction,
                                     std::string message) {
    (void)state;
    return Result<ExecutionState>::failure(
        internal_diagnostic(instruction.source_line, std::move(message)));
}

Result<ExecutionState> finish_dynamic_error(
    ExecutionState state,
    Diagnostic diagnostic,
    const std::optional<std::size_t> source_line) {
    diagnostic.category = DiagnosticCategory::execution;
    diagnostic.source_line = source_line;
    const OperationResult terminal =
        state.terminate(MachineStatus::error, std::move(diagnostic));
    if (!terminal.ok()) {
        return Result<ExecutionState>::failure(*terminal.diagnostic);
    }
    return Result<ExecutionState>::success(std::move(state));
}

Result<ExecutionState> finish_resource_error(
    ExecutionState state,
    const std::optional<std::size_t> source_line) {
    // ERROR sigue siendo uno de los cuatro estados contractuales; RECURSOS es
    // la categoria que evita presentar un fallo fisico como sintaxis invalida.
    Diagnostic diagnostic{DiagnosticCategory::resource,
                          DiagnosticCode::resource_exhausted,
                          "memoria agotada", source_line};
    const OperationResult terminal =
        state.terminate(MachineStatus::error, std::move(diagnostic));
    if (!terminal.ok()) {
        return Result<ExecutionState>::failure(*terminal.diagnostic);
    }
    return Result<ExecutionState>::success(std::move(state));
}

Result<ExecutionState> fail_from_operation(ExecutionState state,
                                           const Instruction& instruction,
                                           const OperationResult& operation) {
    if (operation.diagnostic->category == DiagnosticCategory::execution) {
        return finish_dynamic_error(std::move(state), *operation.diagnostic,
                                    instruction.source_line);
    }
    return Result<ExecutionState>::failure(*operation.diagnostic);
}

Result<ExecutionState> commit_next(ExecutionState state,
                                   const std::size_t next_program_counter) {
    // k cuenta una transicion efectivamente ejecutada. Solo se incrementa
    // despues de completar la operacion semantica de la instruccion.
    const OperationResult counted = state.record_executed_instruction();
    if (!counted.ok()) {
        return Result<ExecutionState>::failure(*counted.diagnostic);
    }
    const OperationResult advanced =
        state.set_program_counter(next_program_counter);
    if (!advanced.ok()) {
        return Result<ExecutionState>::failure(*advanced.diagnostic);
    }
    return Result<ExecutionState>::success(std::move(state));
}

Result<ExecutionState> execute_instruction(ExecutionState state) {
    // La referencia nace despues de mover el estado a esta transicion, por lo
    // que apunta de forma estable al Program inmutable que pertenece a C.
    const Instruction& instruction =
        state.program().at(state.program_counter());
    const std::size_t source_line = instruction.source_line;
    const std::size_t next_pc = state.program_counter() + 1U;

    try {
    switch (instruction.opcode) {
        case Opcode::constant: {
            const RegisterId* destination =
                operand_as<RegisterId>(instruction, 0U);
            const Immediate* immediate =
                operand_as<Immediate>(instruction, 1U);
            if (destination == nullptr || immediate == nullptr) {
                return fail_internal(std::move(state), instruction,
                                     "CONST carece de operandos tipados validos");
            }
            const OperationResult written =
                state.write_register(*destination, immediate->value);
            if (!written.ok()) {
                return fail_from_operation(std::move(state), instruction,
                                           written);
            }
            return commit_next(std::move(state), next_pc);
        }

        case Opcode::load: {
            const RegisterId* destination =
                operand_as<RegisterId>(instruction, 0U);
            const MemorySlot* slot = operand_as<MemorySlot>(instruction, 1U);
            if (destination == nullptr || slot == nullptr) {
                return fail_internal(std::move(state), instruction,
                                     "LOAD carece de operandos tipados validos");
            }
            const Result<Integer> loaded = state.read_memory(*slot);
            if (!loaded.ok()) {
                return finish_dynamic_error(std::move(state),
                                            *loaded.diagnostic,
                                            instruction.source_line);
            }
            const OperationResult written =
                state.write_register(*destination, *loaded.value);
            if (!written.ok()) {
                return fail_from_operation(std::move(state), instruction,
                                           written);
            }
            return commit_next(std::move(state), next_pc);
        }

        case Opcode::store: {
            const MemorySlot* slot = operand_as<MemorySlot>(instruction, 0U);
            const RegisterId* source = operand_as<RegisterId>(instruction, 1U);
            if (slot == nullptr || source == nullptr) {
                return fail_internal(std::move(state), instruction,
                                     "STORE carece de operandos tipados validos");
            }
            const Result<Integer> value = state.read_register(*source);
            if (!value.ok()) {
                return finish_dynamic_error(std::move(state),
                                            *value.diagnostic,
                                            instruction.source_line);
            }
            const OperationResult written = state.write_memory(*slot, *value.value);
            if (!written.ok()) {
                return fail_from_operation(std::move(state), instruction,
                                           written);
            }
            return commit_next(std::move(state), next_pc);
        }

        case Opcode::move: {
            const RegisterId* destination =
                operand_as<RegisterId>(instruction, 0U);
            const RegisterId* source = operand_as<RegisterId>(instruction, 1U);
            if (destination == nullptr || source == nullptr) {
                return fail_internal(std::move(state), instruction,
                                     "MOV carece de operandos tipados validos");
            }
            // La lectura ocurre antes de la escritura. Esta regla hace que
            // MOV R0,R0 y las copias de cpp_int sean transiciones bien definidas.
            const Result<Integer> value = state.read_register(*source);
            if (!value.ok()) {
                return finish_dynamic_error(std::move(state),
                                            *value.diagnostic,
                                            instruction.source_line);
            }
            const OperationResult written =
                state.write_register(*destination, *value.value);
            if (!written.ok()) {
                return fail_from_operation(std::move(state), instruction,
                                           written);
            }
            return commit_next(std::move(state), next_pc);
        }

        case Opcode::add:
        case Opcode::subtract:
        case Opcode::multiply:
        case Opcode::divide: {
            const RegisterId* destination =
                operand_as<RegisterId>(instruction, 0U);
            const RegisterId* left_register =
                operand_as<RegisterId>(instruction, 1U);
            const RegisterId* right_register =
                operand_as<RegisterId>(instruction, 2U);
            if (destination == nullptr || left_register == nullptr ||
                right_register == nullptr) {
                return fail_internal(
                    std::move(state), instruction,
                    "la instruccion aritmetica carece de operandos tipados validos");
            }

            // Se copian ambos operandos antes de tocar Rd. Por ello una forma
            // como ADD R0,R0,R1 conserva la semantica simultanea de la formula.
            const Result<Integer> left = state.read_register(*left_register);
            if (!left.ok()) {
                return finish_dynamic_error(std::move(state), *left.diagnostic,
                                            instruction.source_line);
            }
            const Result<Integer> right = state.read_register(*right_register);
            if (!right.ok()) {
                return finish_dynamic_error(std::move(state), *right.diagnostic,
                                            instruction.source_line);
            }
            if (instruction.opcode == Opcode::divide && *right.value == 0) {
                return finish_dynamic_error(
                    std::move(state),
                    {DiagnosticCategory::execution,
                     DiagnosticCode::division_by_zero,
                     "DIV no esta definida cuando el divisor es cero"},
                    instruction.source_line);
            }

            Integer result;
            switch (instruction.opcode) {
                case Opcode::add:
                    result = *left.value + *right.value;
                    break;
                case Opcode::subtract:
                    result = *left.value - *right.value;
                    break;
                case Opcode::multiply:
                    result = *left.value * *right.value;
                    break;
                case Opcode::divide:
                    // cpp_int realiza division entera con truncamiento hacia
                    // cero; no se introduce coma flotante ni conversion a 64 bits.
                    result = *left.value / *right.value;
                    break;
                default:
                    return fail_internal(std::move(state), instruction,
                                         "opcode aritmetico interno inesperado");
            }
            const OperationResult written =
                state.write_register(*destination, result);
            if (!written.ok()) {
                return fail_from_operation(std::move(state), instruction,
                                           written);
            }
            return commit_next(std::move(state), next_pc);
        }

        case Opcode::compare_equal:
        case Opcode::compare_not_equal:
        case Opcode::compare_less:
        case Opcode::compare_less_equal:
        case Opcode::compare_greater:
        case Opcode::compare_greater_equal: {
            const RegisterId* destination =
                operand_as<RegisterId>(instruction, 0U);
            const RegisterId* left_register =
                operand_as<RegisterId>(instruction, 1U);
            const RegisterId* right_register =
                operand_as<RegisterId>(instruction, 2U);
            if (destination == nullptr || left_register == nullptr ||
                right_register == nullptr) {
                return fail_internal(
                    std::move(state), instruction,
                    "la comparacion carece de operandos tipados validos");
            }
            const Result<Integer> left = state.read_register(*left_register);
            if (!left.ok()) {
                return finish_dynamic_error(std::move(state), *left.diagnostic,
                                            instruction.source_line);
            }
            const Result<Integer> right = state.read_register(*right_register);
            if (!right.ok()) {
                return finish_dynamic_error(std::move(state), *right.diagnostic,
                                            instruction.source_line);
            }

            bool predicate = false;
            switch (instruction.opcode) {
                case Opcode::compare_equal:
                    predicate = *left.value == *right.value;
                    break;
                case Opcode::compare_not_equal:
                    predicate = *left.value != *right.value;
                    break;
                case Opcode::compare_less:
                    predicate = *left.value < *right.value;
                    break;
                case Opcode::compare_less_equal:
                    predicate = *left.value <= *right.value;
                    break;
                case Opcode::compare_greater:
                    predicate = *left.value > *right.value;
                    break;
                case Opcode::compare_greater_equal:
                    predicate = *left.value >= *right.value;
                    break;
                default:
                    return fail_internal(std::move(state), instruction,
                                         "comparador interno inesperado");
            }
            // La codificacion booleana de LCERO_IR 1 es exactamente 1 o 0;
            // no se guarda la representacion particular de bool del compilador.
            const Integer comparison_value = predicate ? Integer(1) : Integer(0);
            const OperationResult written =
                state.write_register(*destination, comparison_value);
            if (!written.ok()) {
                return fail_from_operation(std::move(state), instruction,
                                           written);
            }
            return commit_next(std::move(state), next_pc);
        }

        case Opcode::jump: {
            const JumpTarget* target =
                operand_as<JumpTarget>(instruction, 0U);
            if (target == nullptr) {
                return fail_internal(std::move(state), instruction,
                                     "JMP carece de destino resuelto");
            }
            // Un salto sustituye pc por el destino; no agrega el incremento
            // ordinario. El LABEL ya fue eliminado al construir P.
            return commit_next(std::move(state), target->instruction_index);
        }

        case Opcode::jump_if_zero: {
            const RegisterId* condition =
                operand_as<RegisterId>(instruction, 0U);
            const JumpTarget* target =
                operand_as<JumpTarget>(instruction, 1U);
            if (condition == nullptr || target == nullptr) {
                return fail_internal(std::move(state), instruction,
                                     "JZ carece de operandos tipados validos");
            }
            const Result<Integer> value = state.read_register(*condition);
            if (!value.ok()) {
                return finish_dynamic_error(std::move(state),
                                            *value.diagnostic,
                                            instruction.source_line);
            }
            const std::size_t selected_pc =
                *value.value == 0 ? target->instruction_index : next_pc;
            return commit_next(std::move(state), selected_pc);
        }

        case Opcode::print: {
            const RegisterId* source =
                operand_as<RegisterId>(instruction, 0U);
            if (source == nullptr) {
                return fail_internal(std::move(state), instruction,
                                     "PRINT carece de registro fuente");
            }
            const Result<Integer> value = state.read_register(*source);
            if (!value.ok()) {
                return finish_dynamic_error(std::move(state),
                                            *value.diagnostic,
                                            instruction.source_line);
            }
            // O conserva el orden observable incluso si una transicion futura
            // termina en ERROR o LIMIT. La serializacion agregara LF exacto.
            state.append_output(*value.value);
            return commit_next(std::move(state), next_pc);
        }

        case Opcode::halt: {
            // HALT es una instruccion ejecutable: suma un paso, pero deja pc en
            // el indice del propio HALT para describir el estado final.
            const OperationResult counted = state.record_executed_instruction();
            if (!counted.ok()) {
                return Result<ExecutionState>::failure(*counted.diagnostic);
            }
            const OperationResult terminal = state.terminate(MachineStatus::halt);
            if (!terminal.ok()) {
                return Result<ExecutionState>::failure(*terminal.diagnostic);
            }
            return Result<ExecutionState>::success(std::move(state));
        }
    }

    return fail_internal(std::move(state), instruction,
                         "opcode interno fuera de la ISA autorizada");
    } catch (const std::bad_alloc&) {
        // Al capturar dentro de la transicion, C sigue disponible y conserva
        // pc, k y cualquier PRINT completado por instrucciones anteriores.
        return finish_resource_error(std::move(state), source_line);
    }
}

}  // namespace

Result<ExecutionState> interpret(ValidatedIr validated_ir,
                                 const ExecutionOptions& options) {
    Result<ExecutionState> created = ExecutionState::create(
        std::move(validated_ir.program), validated_ir.memory_size);
    if (!created.ok()) {
        return created;
    }
    ExecutionState state = std::move(*created.value);

    try {
        while (state.status() == MachineStatus::running) {
            // pc=|P| no designa una instruccion. Por eso se diagnostica la
            // caida antes de consultar un presupuesto para una transicion que
            // no existe.
            if (state.program_counter() >= state.program().size()) {
                Result<ExecutionState> fallthrough = finish_dynamic_error(
                    std::move(state),
                    {DiagnosticCategory::execution,
                     DiagnosticCode::fallthrough_without_halt,
                     "pc alcanzo el final de P sin ejecutar HALT"},
                    std::nullopt);
                if (!fallthrough.ok()) {
                    return fallthrough;
                }
                state = std::move(*fallthrough.value);
                break;
            }

            // El limite N se consulta antes de la proxima instruccion. Asi,
            // N=0 impide el primer paso y un HALT que completa el paso N gana
            // frente a LIMIT porque ya dejo a la maquina en estado terminal.
            if (options.max_steps.has_value() &&
                state.executed_steps() >= *options.max_steps) {
                const OperationResult terminal =
                    state.terminate(MachineStatus::limit);
                if (!terminal.ok()) {
                    return Result<ExecutionState>::failure(*terminal.diagnostic);
                }
                break;
            }

            const std::size_t previous_pc = state.program_counter();
            const std::uint64_t previous_steps = state.executed_steps();
            if (options.trace_writer != nullptr) {
                options.trace_writer->write_before(
                    state, state.program().at(previous_pc));
            }
            Result<ExecutionState> transition =
                execute_instruction(std::move(state));
            if (!transition.ok()) {
                return transition;
            }
            state = std::move(*transition.value);
            if (options.trace_writer != nullptr) {
                // P es inmutable, de modo que previous_pc sigue identificando
                // la instruccion ejecutada incluso despues de un salto.
                options.trace_writer->write_after(
                    state, state.program().at(previous_pc), previous_pc,
                    previous_steps);
            }
        }
    } catch (const std::bad_alloc&) {
        return Result<ExecutionState>::failure(
            {DiagnosticCategory::resource, DiagnosticCode::resource_exhausted,
             "memoria insuficiente durante la ejecucion"});
    } catch (const std::exception& exception) {
        return Result<ExecutionState>::failure(
            {DiagnosticCategory::internal,
             DiagnosticCode::invalid_instruction_state,
             std::string("fallo interno controlado durante la ejecucion: ") +
                 exception.what()});
    }

    return Result<ExecutionState>::success(std::move(state));
}

}  // namespace lcero::vm
