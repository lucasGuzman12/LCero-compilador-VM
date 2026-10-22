#include "lcero/vm/execution_state.hpp"

#include <limits>
#include <new>
#include <utility>

namespace lcero::vm {
namespace {

Diagnostic execution_diagnostic(const DiagnosticCode code,
                                std::string message) {
    return {DiagnosticCategory::execution, code, std::move(message)};
}

Diagnostic internal_diagnostic(const DiagnosticCode code,
                               std::string message) {
    return {DiagnosticCategory::internal, code, std::move(message)};
}

}  // namespace

Result<ExecutionState> ExecutionState::create(
    Program program,
    const MemorySize& memory_size) {
    if (memory_size.value < 0) {
        return Result<ExecutionState>::failure(
            {DiagnosticCategory::load, DiagnosticCode::negative_memory_size,
             "la cota logica MEM no puede ser negativa"});
    }

    try {
        return Result<ExecutionState>::success(
            ExecutionState(std::move(program), memory_size.value));
    } catch (const std::bad_alloc&) {
        // Copiar la cota cpp_int puede requerir memoria fisica aunque MEM sea
        // solo una cota logica. No se convierte ese fallo en gramatica invalida.
        return Result<ExecutionState>::failure(
            {DiagnosticCategory::resource,
             DiagnosticCode::resource_exhausted,
             "memoria agotada"});
    }
}

ExecutionState::ExecutionState(Program program, Integer memory_size)
    : program_(std::move(program)), memory_size_(std::move(memory_size)) {}

const Program& ExecutionState::program() const noexcept {
    return program_;
}

std::size_t ExecutionState::program_counter() const noexcept {
    return program_counter_;
}

std::uint64_t ExecutionState::executed_steps() const noexcept {
    return executed_steps_;
}

const Integer& ExecutionState::memory_size() const noexcept {
    return memory_size_;
}

MachineStatus ExecutionState::status() const noexcept {
    return status_;
}

const std::optional<Diagnostic>& ExecutionState::terminal_diagnostic()
    const noexcept {
    return terminal_diagnostic_;
}

const RegisterFile& ExecutionState::registers() const noexcept {
    return registers_;
}

const SparseMemory& ExecutionState::initialized_memory() const noexcept {
    return memory_;
}

bool ExecutionState::is_valid_register(const RegisterId id) noexcept {
    return static_cast<std::uint8_t>(id) <=
           static_cast<std::uint8_t>(RegisterId::r3);
}

Result<Integer> ExecutionState::read_register(const RegisterId id) const {
    if (!is_valid_register(id)) {
        return Result<Integer>::failure(execution_diagnostic(
            DiagnosticCode::invalid_register,
            "se intento leer un registro fuera de R0..R3"));
    }

    const auto index = static_cast<std::size_t>(id);
    if (!registers_[index].has_value()) {
        return Result<Integer>::failure(execution_diagnostic(
            DiagnosticCode::uninitialized_register,
            "se intento leer un registro aun no inicializado"));
    }

    // El resultado es una copia de cpp_int: modificar lo leido no altera R.
    return Result<Integer>::success(*registers_[index]);
}

OperationResult ExecutionState::write_register(const RegisterId id,
                                               const Integer& value) {
    if (!is_valid_register(id)) {
        return OperationResult::failure(execution_diagnostic(
            DiagnosticCode::invalid_register,
            "se intento escribir un registro fuera de R0..R3"));
    }

    registers_[static_cast<std::size_t>(id)] = value;
    return OperationResult::success();
}

bool ExecutionState::is_slot_in_range(const MemorySlot& slot) const {
    return slot.index >= 0 && slot.index < memory_size_;
}

Result<Integer> ExecutionState::read_memory(const MemorySlot& slot) const {
    if (!is_slot_in_range(slot)) {
        return Result<Integer>::failure(execution_diagnostic(
            DiagnosticCode::slot_out_of_range,
            "el slot debe cumplir 0 <= indice < MEM"));
    }

    const auto found = memory_.find(slot.index);
    if (found == memory_.end()) {
        return Result<Integer>::failure(execution_diagnostic(
            DiagnosticCode::uninitialized_slot,
            "se intento leer un slot valido aun no inicializado"));
    }

    return Result<Integer>::success(found->second);
}

OperationResult ExecutionState::write_memory(const MemorySlot& slot,
                                             const Integer& value) {
    if (!is_slot_in_range(slot)) {
        return OperationResult::failure(execution_diagnostic(
            DiagnosticCode::slot_out_of_range,
            "el slot debe cumplir 0 <= indice < MEM"));
    }

    memory_[slot.index] = value;
    return OperationResult::success();
}

std::size_t ExecutionState::initialized_slot_count() const noexcept {
    return memory_.size();
}

void ExecutionState::append_output(const Integer& value) {
    output_.push_back(value);
}

const std::vector<Integer>& ExecutionState::output_values() const noexcept {
    return output_;
}

std::string ExecutionState::output_text() const {
    std::string text;
    for (const Integer& value : output_) {
        text += to_decimal_string(value);
        text.push_back('\n');
    }
    return text;
}

OperationResult ExecutionState::set_program_counter(const std::size_t target) {
    // pc=|P| es representable porque una etiqueta final puede resolver alli;
    // intentar ejecutar ese pc se diagnosticara en la etapa del interprete.
    if (target > program_.size()) {
        return OperationResult::failure(internal_diagnostic(
            DiagnosticCode::program_counter_out_of_range,
            "pc no puede superar la longitud de P"));
    }

    program_counter_ = target;
    return OperationResult::success();
}

OperationResult ExecutionState::record_executed_instruction() {
    if (status_ != MachineStatus::running) {
        return OperationResult::failure(internal_diagnostic(
            DiagnosticCode::invalid_terminal_transition,
            "k no puede crecer despues de un estado terminal"));
    }

    if (executed_steps_ == std::numeric_limits<std::uint64_t>::max()) {
        return OperationResult::failure(internal_diagnostic(
            DiagnosticCode::step_counter_overflow,
            "el contador fisico de pasos no puede representarse"));
    }

    ++executed_steps_;
    return OperationResult::success();
}

OperationResult ExecutionState::terminate(
    const MachineStatus terminal_status,
    std::optional<Diagnostic> diagnostic) {
    if (status_ != MachineStatus::running ||
        terminal_status == MachineStatus::running) {
        return OperationResult::failure(internal_diagnostic(
            DiagnosticCode::invalid_terminal_transition,
            "solo se permite una transicion de running a un estado terminal"));
    }

    const bool requires_diagnostic =
        terminal_status == MachineStatus::error ||
        terminal_status == MachineStatus::load_error;
    if (requires_diagnostic != diagnostic.has_value()) {
        return OperationResult::failure(internal_diagnostic(
            DiagnosticCode::invalid_terminal_transition,
            "ERROR y ERROR_DE_CARGA requieren diagnostico; HALT y LIMIT no"));
    }

    status_ = terminal_status;
    terminal_diagnostic_ = std::move(diagnostic);
    return OperationResult::success();
}

std::string_view machine_status_name(const MachineStatus status) noexcept {
    switch (status) {
        case MachineStatus::running:
            return "RUNNING";
        case MachineStatus::halt:
            return "HALT";
        case MachineStatus::error:
            return "ERROR";
        case MachineStatus::load_error:
            return "ERROR_DE_CARGA";
        case MachineStatus::limit:
            return "LIMIT";
    }

    return "ERROR_INTERNO";
}

}  // namespace lcero::vm
