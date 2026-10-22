#pragma once

#include "lcero/common/diagnostic.hpp"
#include "lcero/compiler_options.hpp"

#include <cstddef>

namespace lcero {

enum class CompilationStatus {
    Failed,
    Succeeded
};

struct CompilationResult {
    CompilationStatus status = CompilationStatus::Failed;
    BackendKind backend = BackendKind::Reference;
    bool source_read_completed = false;
    std::size_t source_bytes = 0;
    bool lexical_analysis_completed = false;
    std::size_t token_count = 0;
    std::size_t nfa_state_count = 0;
    std::size_t determinized_state_count = 0;
    std::size_t minimized_state_count = 0;
    bool syntactic_analysis_completed = false;
    std::size_t declaration_count = 0;
    std::size_t statement_count = 0;
    bool semantic_analysis_completed = false;
    std::size_t symbol_count = 0;
    std::size_t initialized_variable_count = 0;
    bool ir_generation_completed = false;
    std::size_t ir_instruction_count = 0;
    std::size_t virtual_temporary_count = 0;
    std::size_t label_count = 0;
    bool lci_emission_completed = false;
    std::size_t memory_slot_count = 0;
    std::size_t emitted_instruction_count = 0;
    bool optimization_completed = false;
    std::size_t cfg_block_count = 0;
    std::size_t cfg_edge_count = 0;
    std::size_t interference_vertex_count = 0;
    std::size_t interference_edge_count = 0;
    std::size_t registers_used = 0;
    std::size_t spilled_temporary_count = 0;
    std::size_t load_count = 0;
    std::size_t store_count = 0;
    std::size_t liveness_iterations = 0;
};

// Punto de coordinacion del pipeline: invalida la salida anterior, analiza el
// fuente, construye la RI, la baja al contrato LCERO_IR 1, valida el texto y lo
// publica de manera transaccional.
class Compiler {
public:
    [[nodiscard]] CompilationResult run(
        const CompilerOptions& options,
        common::DiagnosticBag& diagnostics) const;
};

} // namespace lcero
