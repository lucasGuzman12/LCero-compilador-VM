#include "lcero/compiler.hpp"

#include "lcero/common/output_artifact.hpp"
#include "lcero/common/source_reader.hpp"
#include "lcero/ir/generator.hpp"
#include "lcero/ir/lci_validator.hpp"
#include "lcero/ir/optimized_backend.hpp"
#include "lcero/ir/reference_backend.hpp"
#include "lcero/lexer/lexer.hpp"
#include "lcero/parser/parser.hpp"
#include "lcero/semantic/semantic_analyzer.hpp"

namespace lcero {

CompilationResult Compiler::run(
    const CompilerOptions& options,
    common::DiagnosticBag& diagnostics) const {
    CompilationResult result;
    result.backend = options.backend;

    const common::OutputArtifact output_artifact;
    if (!output_artifact.invalidate_previous(options.output_path, diagnostics)) {
        return result;
    }

    const common::SourceReader source_reader;
    const auto source = source_reader.read(options.input_path, diagnostics);
    if (!source.has_value()) {
        return result;
    }
    result.source_read_completed = true;
    result.source_bytes = source->content.size();

    const lexer::Lexer lexer;
    const auto lex_result = lexer.tokenize(source.value(), diagnostics);
    const auto& automata = lexer.automata();
    result.token_count = lex_result.tokens.size();
    result.nfa_state_count = automata.nfa().state_count();
    result.determinized_state_count = automata.determinized_dfa().state_count();
    result.minimized_state_count = automata.minimized_dfa().state_count();
    if (!lex_result.succeeded) {
        return result;
    }
    result.lexical_analysis_completed = true;

    parser::Parser parser;
    auto parse_result = parser.parse(lex_result.tokens, source->path, diagnostics);
    if (!parse_result.succeeded) {
        return result;
    }
    result.syntactic_analysis_completed = true;
    result.declaration_count = parse_result.program->declarations.size();
    result.statement_count = parse_result.program->statements.size();

    semantic::SemanticAnalyzer semantic_analyzer;
    auto semantic_result = semantic_analyzer.analyze(
        *parse_result.program, source->path, diagnostics);
    result.symbol_count = semantic_result.symbol_table.size();
    result.initialized_variable_count =
        semantic_result.definitely_initialized.size();
    if (!semantic_result.succeeded) {
        return result;
    }
    result.semantic_analysis_completed = true;

    ir::Generator ir_generator;
    auto generation_result = ir_generator.generate(
        *parse_result.program,
        semantic_result.symbol_table,
        source->path,
        diagnostics);
    if (!generation_result.succeeded) {
        return result;
    }
    result.ir_generation_completed = true;
    result.ir_instruction_count = generation_result.program.instructions.size();
    result.virtual_temporary_count = generation_result.program.temporary_count;
    result.label_count = generation_result.program.label_count;

    ir::BackendResult backend_result;
    if (options.backend == BackendKind::Optimized) {
        const ir::OptimizedBackend backend;
        backend_result = backend.emit(
            generation_result.program, options.output_path, diagnostics);
    } else {
        const ir::ReferenceBackend backend;
        backend_result = backend.emit(
            generation_result.program, options.output_path, diagnostics);
    }
    if (!backend_result.succeeded) {
        return result;
    }
    result.optimization_completed = options.backend == BackendKind::Optimized;
    result.memory_slot_count = backend_result.memory_slot_count;
    result.emitted_instruction_count = backend_result.physical_instruction_count;
    result.cfg_block_count = backend_result.cfg_block_count;
    result.cfg_edge_count = backend_result.cfg_edge_count;
    result.interference_vertex_count = backend_result.interference_vertex_count;
    result.interference_edge_count = backend_result.interference_edge_count;
    result.registers_used = backend_result.registers_used;
    result.spilled_temporary_count = backend_result.spilled_temporary_count;
    result.load_count = backend_result.load_count;
    result.store_count = backend_result.store_count;
    result.liveness_iterations = backend_result.liveness_iterations;

    const ir::LciValidator validator;
    if (!validator.validate(backend_result.text, options.output_path, diagnostics)) {
        return result;
    }
    if (!output_artifact.publish(
            options.output_path, backend_result.text, diagnostics)) {
        return result;
    }

    result.lci_emission_completed = true;
    result.status = CompilationStatus::Succeeded;
    return result;
}

} // namespace lcero
