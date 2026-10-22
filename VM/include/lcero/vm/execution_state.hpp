#pragma once

#include "lcero/vm/diagnostics.hpp"
#include "lcero/vm/instruction.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lcero::vm {

using RegisterFile = std::array<std::optional<Integer>, 4>;
using SparseMemory = std::map<Integer, Integer>;

// running es el estado previo a una terminacion. Los otros cuatro nombres son
// exactamente los resultados operacionales previstos por el contrato.
enum class MachineStatus {
    running,
    halt,
    error,
    load_error,
    limit,
};

class ExecutionState {
public:
    // La fabrica valida la cota logica antes de construir C. Una MEM negativa
    // se informa como dato de error y no como una excepcion que escape.
    [[nodiscard]] static Result<ExecutionState> create(
        Program program,
        const MemorySize& memory_size);

    [[nodiscard]] const Program& program() const noexcept;
    [[nodiscard]] std::size_t program_counter() const noexcept;
    [[nodiscard]] std::uint64_t executed_steps() const noexcept;
    [[nodiscard]] const Integer& memory_size() const noexcept;
    [[nodiscard]] MachineStatus status() const noexcept;
    [[nodiscard]] const std::optional<Diagnostic>& terminal_diagnostic()
        const noexcept;

    // Estas vistas const son una API de observabilidad: Visual Studio y el
    // trazador pueden inspeccionar R y M sin abrir mutadores que alteren C.
    [[nodiscard]] const RegisterFile& registers() const noexcept;
    [[nodiscard]] const SparseMemory& initialized_memory() const noexcept;

    // Un registro valido puede estar ausente o contener incluso el valor cero;
    // read_register devuelve diagnosticos diferentes para invalido y ausente.
    [[nodiscard]] Result<Integer> read_register(RegisterId id) const;
    [[nodiscard]] OperationResult write_register(RegisterId id,
                                                 const Integer& value);

    // La memoria es dispersa: solo se materializan slots escritos. La cota MEM
    // sigue siendo cpp_int, por lo que una declaracion enorme no reserva n celdas.
    [[nodiscard]] Result<Integer> read_memory(const MemorySlot& slot) const;
    [[nodiscard]] OperationResult write_memory(const MemorySlot& slot,
                                               const Integer& value);
    [[nodiscard]] std::size_t initialized_slot_count() const noexcept;

    // O se acumula de manera incremental en memoria. output_text agrega LF de
    // forma explicita y nunca escribe trazas ni valores en stdout por si sola.
    void append_output(const Integer& value);
    [[nodiscard]] const std::vector<Integer>& output_values() const noexcept;
    [[nodiscard]] std::string output_text() const;

    // El interprete usa estas operaciones para mantener los invariantes de pc,
    // k y terminacion en cada transicion de la maquina abstracta.
    [[nodiscard]] OperationResult set_program_counter(std::size_t target);
    [[nodiscard]] OperationResult record_executed_instruction();
    [[nodiscard]] OperationResult terminate(
        MachineStatus terminal_status,
        std::optional<Diagnostic> diagnostic = std::nullopt);

private:
    ExecutionState(Program program, Integer memory_size);

    [[nodiscard]] static bool is_valid_register(RegisterId id) noexcept;
    [[nodiscard]] bool is_slot_in_range(const MemorySlot& slot) const;

    Program program_;
    std::size_t program_counter_ = 0;
    RegisterFile registers_{};
    Integer memory_size_;
    SparseMemory memory_;
    std::vector<Integer> output_;
    std::uint64_t executed_steps_ = 0;
    MachineStatus status_ = MachineStatus::running;
    std::optional<Diagnostic> terminal_diagnostic_;
};

[[nodiscard]] std::string_view machine_status_name(
    MachineStatus status) noexcept;

}  // namespace lcero::vm
