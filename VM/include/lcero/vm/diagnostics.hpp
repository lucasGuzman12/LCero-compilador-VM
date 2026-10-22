#pragma once

#include <optional>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace lcero::vm {

// La categoria separa errores del texto de RI, errores durante una transicion
// y violaciones internas. Esta clasificacion no agrega estados a LCERO_IR 1.
enum class DiagnosticCategory {
    load,
    execution,
    internal,
    resource,
};

// Los codigos permiten que los tests distingan condiciones semanticamente
// diferentes sin depender de la redaccion humana del mensaje.
enum class DiagnosticCode {
    invalid_register,
    uninitialized_register,
    negative_memory_size,
    slot_out_of_range,
    uninitialized_slot,
    invalid_immediate_syntax,
    invalid_memory_size_syntax,
    invalid_slot_syntax,
    program_counter_out_of_range,
    step_counter_overflow,
    invalid_terminal_transition,
    file_open_failed,
    file_read_failed,
    file_too_large,
    resource_exhausted,
    invalid_character,
    invalid_line_ending,
    invalid_header,
    missing_memory_declaration,
    invalid_instruction_format,
    unknown_opcode,
    invalid_arity,
    invalid_operand,
    invalid_label,
    reserved_label,
    duplicate_label,
    unknown_label,
    missing_halt,
    division_by_zero,
    fallthrough_without_halt,
    invalid_instruction_state,
    output_write_failed,
    trace_open_failed,
    trace_write_failed,
};

struct Diagnostic {
    DiagnosticCategory category;
    DiagnosticCode code;
    std::string message;
    // La linea es 1-based y solo aparece cuando el problema pertenece a una
    // posicion concreta del archivo .lci.
    std::optional<std::size_t> source_line = std::nullopt;
};

// Result expresa los errores conocidos como datos. Asi, un programa incorrecto
// no depende de una excepcion sin tratar para detener la maquina virtual.
template <typename T>
struct Result {
    std::optional<T> value;
    std::optional<Diagnostic> diagnostic;

    [[nodiscard]] bool ok() const noexcept {
        return value.has_value() && !diagnostic.has_value();
    }

    static Result success(T result) {
        return {std::optional<T>(std::move(result)), std::nullopt};
    }

    static Result failure(Diagnostic error) {
        return {std::nullopt,
                std::optional<Diagnostic>(std::move(error))};
    }
};

// Algunas operaciones solo cambian estado y no devuelven un valor adicional.
// OperationResult conserva el mismo protocolo explicito que Result<T>.
struct OperationResult {
    std::optional<Diagnostic> diagnostic;

    [[nodiscard]] bool ok() const noexcept {
        return !diagnostic.has_value();
    }

    static OperationResult success() noexcept;
    static OperationResult failure(Diagnostic error);
};

[[nodiscard]] std::string_view diagnostic_category_name(
    DiagnosticCategory category) noexcept;
[[nodiscard]] std::string_view diagnostic_code_name(
    DiagnosticCode code) noexcept;

}  // namespace lcero::vm
