#include "lcero/vm/diagnostics.hpp"

#include <utility>

namespace lcero::vm {

OperationResult OperationResult::success() noexcept {
    return {std::nullopt};
}

OperationResult OperationResult::failure(Diagnostic error) {
    return {std::optional<Diagnostic>(std::move(error))};
}

std::string_view diagnostic_category_name(
    const DiagnosticCategory category) noexcept {
    switch (category) {
        case DiagnosticCategory::load:
            return "ERROR_DE_CARGA";
        case DiagnosticCategory::execution:
            return "ERROR";
        case DiagnosticCategory::internal:
            return "ERROR_INTERNO";
        case DiagnosticCategory::resource:
            return "ERROR_RECURSO";
    }

    return "ERROR_INTERNO";
}

std::string_view diagnostic_code_name(const DiagnosticCode code) noexcept {
    switch (code) {
        case DiagnosticCode::invalid_register:
            return "REGISTRO_INVALIDO";
        case DiagnosticCode::uninitialized_register:
            return "REGISTRO_NO_INICIALIZADO";
        case DiagnosticCode::negative_memory_size:
            return "MEM_NEGATIVA";
        case DiagnosticCode::slot_out_of_range:
            return "SLOT_FUERA_DE_RANGO";
        case DiagnosticCode::uninitialized_slot:
            return "SLOT_NO_INICIALIZADO";
        case DiagnosticCode::invalid_immediate_syntax:
            return "INMEDIATO_INVALIDO";
        case DiagnosticCode::invalid_memory_size_syntax:
            return "MEM_INVALIDA";
        case DiagnosticCode::invalid_slot_syntax:
            return "INDICE_INVALIDO";
        case DiagnosticCode::program_counter_out_of_range:
            return "PC_FUERA_DE_RANGO";
        case DiagnosticCode::step_counter_overflow:
            return "CONTADOR_DE_PASOS_DESBORDADO";
        case DiagnosticCode::invalid_terminal_transition:
            return "TRANSICION_TERMINAL_INVALIDA";
        case DiagnosticCode::file_open_failed:
            return "ARCHIVO_INACCESIBLE";
        case DiagnosticCode::file_read_failed:
            return "LECTURA_FALLIDA";
        case DiagnosticCode::file_too_large:
            return "ARCHIVO_DEMASIADO_GRANDE";
        case DiagnosticCode::resource_exhausted:
            return "RECURSO_AGOTADO";
        case DiagnosticCode::invalid_character:
            return "CARACTER_INVALIDO";
        case DiagnosticCode::invalid_line_ending:
            return "FIN_DE_LINEA_INVALIDO";
        case DiagnosticCode::invalid_header:
            return "VERSION_INVALIDA";
        case DiagnosticCode::missing_memory_declaration:
            return "MEM_AUSENTE";
        case DiagnosticCode::invalid_instruction_format:
            return "FORMATO_INVALIDO";
        case DiagnosticCode::unknown_opcode:
            return "OPCODE_DESCONOCIDO";
        case DiagnosticCode::invalid_arity:
            return "ARIDAD_INVALIDA";
        case DiagnosticCode::invalid_operand:
            return "OPERANDO_INVALIDO";
        case DiagnosticCode::invalid_label:
            return "ETIQUETA_INVALIDA";
        case DiagnosticCode::reserved_label:
            return "ETIQUETA_RESERVADA";
        case DiagnosticCode::duplicate_label:
            return "ETIQUETA_DUPLICADA";
        case DiagnosticCode::unknown_label:
            return "ETIQUETA_INEXISTENTE";
        case DiagnosticCode::missing_halt:
            return "HALT_AUSENTE";
        case DiagnosticCode::division_by_zero:
            return "DIVISION_POR_CERO";
        case DiagnosticCode::fallthrough_without_halt:
            return "CAIDA_SIN_HALT";
        case DiagnosticCode::invalid_instruction_state:
            return "INSTRUCCION_INTERNA_INVALIDA";
        case DiagnosticCode::output_write_failed:
            return "ESCRITURA_DE_SALIDA_FALLIDA";
        case DiagnosticCode::trace_open_failed:
            return "TRAZA_INACCESIBLE";
        case DiagnosticCode::trace_write_failed:
            return "ESCRITURA_DE_TRAZA_FALLIDA";
    }

    return "DIAGNOSTICO_DESCONOCIDO";
}

}  // namespace lcero::vm
