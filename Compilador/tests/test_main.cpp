#include "lcero/ast/ast.hpp"
#include "lcero/common/output_artifact.hpp"
#include "lcero/common/path_resolver.hpp"
#include "lcero/common/source_reader.hpp"
#include "lcero/compiler.hpp"
#include "lcero/compiler_options.hpp"
#include "lcero/console_menu.hpp"
#include "lcero/ir/generator.hpp"
#include "lcero/ir/lci_validator.hpp"
#include "lcero/ir/optimized_backend.hpp"
#include "lcero/ir/reference_backend.hpp"
#include "lcero/ir/register_allocation.hpp"
#include "lcero/lexer/automata.hpp"
#include "lcero/lexer/lexer.hpp"
#include "lcero/lexer/token.hpp"
#include "lcero/parser/grammar.hpp"
#include "lcero/parser/parser.hpp"
#include "lcero/semantic/semantic_analyzer.hpp"
#include "lcero/semantic/symbol_table.hpp"

#include <algorithm>
#include <chrono>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto seed = std::chrono::high_resolution_clock::now()
                              .time_since_epoch()
                              .count();
        path_ = std::filesystem::temp_directory_path() /
            ("lcero_module0_tests_" + std::to_string(seed));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text(const std::filesystem::path& path, const std::string& content) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
    if (!output) {
        throw std::runtime_error("No se pudo preparar un archivo de prueba.");
    }
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void create_project_marker(const std::filesystem::path& root) {
    write_text(root / "LCero-Compilador.vcxproj", "<Project />");
}

void test_reads_existing_source() {
    TemporaryDirectory temporary;
    const auto source_path = temporary.path() / "programa.lc";
    write_text(source_path, "print(5);\n");

    lcero::common::DiagnosticBag diagnostics;
    const lcero::common::SourceReader reader;
    const auto source = reader.read(source_path, diagnostics);

    require(source.has_value(), "Debia leer un fuente existente.");
    require(source->content == "print(5);\n", "El contenido leido no coincide.");
    require(!diagnostics.has_errors(), "La lectura valida produjo errores.");
}

void test_reports_missing_source() {
    TemporaryDirectory temporary;
    lcero::common::DiagnosticBag diagnostics;
    const lcero::common::SourceReader reader;

    const auto source = reader.read(temporary.path() / "faltante.lc", diagnostics);

    require(!source.has_value(), "No debia aceptar un archivo inexistente.");
    require(diagnostics.has_errors(), "Falto diagnostico para archivo inexistente.");
}

void test_reports_non_regular_source() {
    TemporaryDirectory temporary;
    const auto directory = temporary.path() / "directorio.lc";
    std::filesystem::create_directories(directory);

    lcero::common::DiagnosticBag diagnostics;
    const lcero::common::SourceReader reader;
    const auto source = reader.read(directory, diagnostics);

    require(!source.has_value(), "No debia leer un directorio como fuente.");
    require(diagnostics.has_errors(), "Falto diagnostico de lectura invalida.");
}

void test_uses_default_paths() {
    TemporaryDirectory temporary;
    create_project_marker(temporary.path());

    const lcero::CommandLineParser parser;
    const auto result = parser.parse({}, temporary.path(), temporary.path());

    require(result.options.has_value(), "Las opciones predeterminadas son validas.");
    require(
        result.options->input_path == temporary.path() / "Source/programa.lc",
        "La entrada predeterminada es incorrecta.");
    require(
        result.options->output_path == temporary.path() / "LCIs/programa.lci",
        "La salida predeterminada es incorrecta.");
}

void test_uses_argument_paths() {
    TemporaryDirectory temporary;
    const auto working_directory = temporary.path() / "trabajo";
    std::filesystem::create_directories(working_directory);

    const lcero::CommandLineParser parser;
    const auto result = parser.parse(
        {"fuentes/demo.lc", "-o", "salidas/demo.lci"},
        temporary.path(),
        working_directory);

    require(result.options.has_value(), "Las rutas explicitas debian ser validas.");
    require(
        result.options->input_path == working_directory / "fuentes/demo.lc",
        "La entrada explicita no se resolvio desde el directorio actual.");
    require(
        result.options->output_path == working_directory / "salidas/demo.lci",
        "La salida explicita no se resolvio desde el directorio actual.");
}

void test_derives_output_for_input_only() {
    TemporaryDirectory temporary;
    const lcero::CommandLineParser parser;
    const auto result = parser.parse(
        {"fuentes/ejemplo.lc"},
        temporary.path(),
        temporary.path());

    require(result.options.has_value(), "La entrada sin -o debia ser valida.");
    require(
        result.options->output_path == temporary.path() / "LCIs/ejemplo.lci",
        "La salida derivada no coincide con el nombre de la entrada.");
}

void test_rejects_invalid_arguments() {
    TemporaryDirectory temporary;
    const lcero::CommandLineParser parser;

    const auto duplicate_input = parser.parse(
        {"uno.lc", "dos.lc"},
        temporary.path(),
        temporary.path());
    require(
        duplicate_input.diagnostics.has_errors(),
        "Debia rechazar dos archivos de entrada.");

    const auto missing_output = parser.parse(
        {"uno.lc", "-o"},
        temporary.path(),
        temporary.path());
    require(
        missing_output.diagnostics.has_errors(),
        "Debia rechazar -o sin una ruta.");

    const auto wrong_extension = parser.parse(
        {"uno.txt"},
        temporary.path(),
        temporary.path());
    require(
        wrong_extension.diagnostics.has_errors(),
        "Debia rechazar una extension de entrada distinta de .lc.");
}

void test_finds_root_from_working_directory() {
    TemporaryDirectory temporary;
    create_project_marker(temporary.path());
    const auto nested = temporary.path() / "uno/dos/tres";
    std::filesystem::create_directories(nested);

    const auto root = lcero::common::PathResolver::find_project_root(nested, {});

    require(root.has_value(), "No se encontro la raiz desde una subcarpeta.");
    require(root.value() == temporary.path(), "Se encontro una raiz incorrecta.");
}

void test_finds_root_from_executable() {
    TemporaryDirectory temporary;
    create_project_marker(temporary.path());
    const auto unrelated = temporary.path().parent_path();
    const auto executable = temporary.path() / "bin/x64/Debug/LCero.Compiler.exe";

    const auto root = lcero::common::PathResolver::find_project_root(
        unrelated,
        executable);

    require(root.has_value(), "No se encontro la raiz desde el ejecutable.");
    require(root.value() == temporary.path(), "La raiz del ejecutable es incorrecta.");
}

void test_compiler_generates_valid_lci() {
    TemporaryDirectory temporary;
    const auto source_path = temporary.path() / "Source/programa.lc";
    const auto output_path = temporary.path() / "LCIs/programa.lci";
    write_text(source_path, "print(5);\n");
    write_text(output_path, "salida anterior");

    lcero::common::DiagnosticBag diagnostics;
    const lcero::Compiler compiler;
    const auto result = compiler.run(
        lcero::CompilerOptions{source_path, output_path},
        diagnostics);

    require(
        result.status == lcero::CompilationStatus::Succeeded,
        "El pipeline completo debia finalizar correctamente.");
    require(result.source_bytes == 10, "El conteo de bytes leidos es incorrecto.");
    require(result.lexical_analysis_completed,
            "La etapa lexica valida debia completarse.");
    require(result.syntactic_analysis_completed,
            "La etapa sintactica valida debia completarse.");
    require(result.semantic_analysis_completed,
            "La etapa semantica valida debia completarse.");
    require(result.ir_generation_completed && result.lci_emission_completed,
            "La RI y la emision validas debian completarse.");
    require(result.declaration_count == 0 && result.statement_count == 1,
            "El resumen del AST integrado es incorrecto.");
    require(result.ir_instruction_count == 3 &&
                result.virtual_temporary_count == 1 &&
                result.label_count == 0,
            "El resumen de RI integrado es incorrecto.");
    require(result.memory_slot_count == 1 &&
                result.emitted_instruction_count == 5,
            "El resumen de emision integrado es incorrecto.");
    require(!diagnostics.has_errors(), "La compilacion valida produjo errores.");
    require(std::filesystem::exists(output_path), "No se publico el .lci valido.");
    require(
        read_text(output_path) ==
            "LCERO_IR 1\n"
            "MEM 1\n"
            "CONST R0, 5\n"
            "STORE 0, R0\n"
            "LOAD R0, 0\n"
            "PRINT R0\n"
            "HALT\n",
        "La emision de referencia simple no es exacta.");
    require(
        !std::filesystem::exists(
            lcero::common::OutputArtifact::temporary_path_for(output_path)),
        "Quedo un archivo temporal accidental.");
}

void test_compiler_stops_on_syntactic_error() {
    TemporaryDirectory temporary;
    const auto source_path = temporary.path() / "Source/invalido_sintactico.lc";
    const auto output_path = temporary.path() / "LCIs/invalido_sintactico.lci";
    write_text(source_path, "print(1)\n");
    write_text(output_path, "salida anterior");

    lcero::common::DiagnosticBag diagnostics;
    const lcero::Compiler compiler;
    const auto result = compiler.run(
        lcero::CompilerOptions{source_path, output_path},
        diagnostics);

    require(result.status == lcero::CompilationStatus::Failed,
            "Un error sintactico debe fallar la ejecucion coordinada.");
    require(result.lexical_analysis_completed,
            "El lexico debia completarse antes del error sintactico.");
    require(!result.syntactic_analysis_completed,
            "La etapa sintactica no debe marcarse completa tras un error.");
    require(diagnostics.has_errors(), "Falto el diagnostico sintactico integrado.");
    require(
        diagnostics.diagnostics().back().category ==
            lcero::common::DiagnosticCategory::Syntactic,
        "El coordinador debia conservar la categoria sintactica.");
    require(!std::filesystem::exists(output_path),
            "Un error sintactico no debe conservar ni crear un .lci.");
}

void test_compiler_stops_on_lexical_error() {
    TemporaryDirectory temporary;
    const auto source_path = temporary.path() / "Source/invalido.lc";
    const auto output_path = temporary.path() / "LCIs/invalido.lci";
    write_text(source_path, "print(@);\n");
    write_text(output_path, "salida anterior");

    lcero::common::DiagnosticBag diagnostics;
    const lcero::Compiler compiler;
    const auto result = compiler.run(
        lcero::CompilerOptions{source_path, output_path},
        diagnostics);

    require(result.status == lcero::CompilationStatus::Failed,
            "Un error lexico debe fallar la ejecucion coordinada.");
    require(!result.lexical_analysis_completed,
            "La etapa lexica no debe marcarse completa tras un error.");
    require(diagnostics.has_errors(), "Falto el diagnostico lexico integrado.");
    require(
        diagnostics.diagnostics().back().category ==
            lcero::common::DiagnosticCategory::Lexical,
        "El coordinador debia conservar la categoria lexica.");
    require(!std::filesystem::exists(output_path),
            "Un error lexico no debe conservar ni crear un .lci.");
}

void test_publishes_output_transactionally() {
    TemporaryDirectory temporary;
    const auto output_path = temporary.path() / "LCIs/prueba.lci";
    lcero::common::DiagnosticBag diagnostics;
    const lcero::common::OutputArtifact artifact;

    require(
        artifact.invalidate_previous(output_path, diagnostics),
        "No se pudo preparar la salida de prueba.");
    require(
        artifact.publish(output_path, "LCERO_IR 1\nMEM 0\nHALT\n", diagnostics),
        "No se pudo publicar el artefacto de prueba.");
    require(!diagnostics.has_errors(), "La publicacion valida produjo errores.");
    require(
        read_text(output_path) == "LCERO_IR 1\nMEM 0\nHALT\n",
        "El artefacto publicado no coincide.");
    require(
        !std::filesystem::exists(
            lcero::common::OutputArtifact::temporary_path_for(output_path)),
        "El temporal debio desaparecer despues de publicar.");
}

void test_reports_output_write_error_without_partial_file() {
    TemporaryDirectory temporary;
    const auto blocking_parent = temporary.path() / "no_es_directorio";
    const auto output_path = blocking_parent / "programa.lci";
    write_text(blocking_parent, "archivo que bloquea el directorio");

    lcero::common::DiagnosticBag diagnostics;
    const lcero::common::OutputArtifact artifact;
    require(!artifact.publish(
                output_path,
                "LCERO_IR 1\nMEM 0\nHALT\n",
                diagnostics),
            "La publicacion debia fallar si el padre no es un directorio.");
    require(diagnostics.has_errors(),
            "Falto el diagnostico del error de escritura.");
    require(!std::filesystem::exists(output_path) &&
                !std::filesystem::exists(
                    lcero::common::OutputArtifact::temporary_path_for(output_path)),
            "Un error de escritura dejo una salida parcial.");
}

lcero::lexer::LexResult tokenize(
    const std::string& source,
    lcero::common::DiagnosticBag& diagnostics) {
    const lcero::lexer::Lexer lexer;
    return lexer.tokenize(
        lcero::common::SourceFile{"prueba.lc", source},
        diagnostics);
}

lcero::parser::ParseResult parse_text(
    const std::string& source,
    lcero::common::DiagnosticBag& diagnostics) {
    const auto lex_result = tokenize(source, diagnostics);
    require(lex_result.succeeded, "El texto de prueba debia ser lexicamente valido.");
    lcero::parser::Parser parser;
    return parser.parse(lex_result.tokens, "prueba.lc", diagnostics);
}

lcero::semantic::SemanticAnalysisResult analyze_text(
    const std::string& source,
    lcero::common::DiagnosticBag& diagnostics) {
    const auto parse_result = parse_text(source, diagnostics);
    require(parse_result.succeeded && parse_result.program != nullptr,
            "El texto de prueba semantica debia ser sintacticamente valido.");
    lcero::semantic::SemanticAnalyzer analyzer;
    return analyzer.analyze(*parse_result.program, "prueba.lc", diagnostics);
}

lcero::ir::GenerationResult generate_ir(
    const std::string& source,
    lcero::common::DiagnosticBag& diagnostics) {
    auto parse_result = parse_text(source, diagnostics);
    require(parse_result.succeeded && parse_result.program != nullptr,
            "El texto de prueba de RI debia ser sintacticamente valido.");
    lcero::semantic::SemanticAnalyzer semantic_analyzer;
    const auto semantic_result = semantic_analyzer.analyze(
        *parse_result.program, "prueba.lc", diagnostics);
    require(semantic_result.succeeded,
            "El texto de prueba de RI debia ser semanticamente valido.");
    lcero::ir::Generator generator;
    return generator.generate(
        *parse_result.program,
        semantic_result.symbol_table,
        "prueba.lc",
        diagnostics);
}

lcero::ir::BackendResult emit_lci(
    const std::string& source,
    lcero::common::DiagnosticBag& diagnostics) {
    const auto generation = generate_ir(source, diagnostics);
    require(generation.succeeded, "La generacion de RI debia ser valida.");
    const lcero::ir::ReferenceBackend backend;
    return backend.emit(generation.program, "prueba.lci", diagnostics);
}

lcero::ir::BackendResult emit_optimized_lci(
    const std::string& source,
    lcero::common::DiagnosticBag& diagnostics) {
    const auto generation = generate_ir(source, diagnostics);
    require(generation.succeeded, "La generacion de RI debia ser valida.");
    const lcero::ir::OptimizedBackend backend;
    return backend.emit(generation.program, "prueba_optimizada.lci", diagnostics);
}

void require_semantic_error(
    const std::string& source,
    const std::string& expected_fragment) {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = analyze_text(source, diagnostics);
    require(!result.succeeded, "El programa debia fallar semanticamente.");
    require(diagnostics.diagnostics().size() == 1,
            "El analizador semantico debe detenerse en el primer error.");
    const auto& diagnostic = diagnostics.diagnostics().front();
    require(diagnostic.category == lcero::common::DiagnosticCategory::Semantic,
            "El error debia tener categoria semantica.");
    require(diagnostic.message.find(expected_fragment) != std::string::npos,
            "El diagnostico semantico no contiene el detalle esperado.");
    require(diagnostic.location.has_value(),
            "El diagnostico semantico debe incluir ubicacion.");
}

void require_token_types(
    const lcero::lexer::LexResult& result,
    const std::vector<lcero::lexer::TokenType>& expected) {
    require(result.succeeded, "La tokenizacion debia finalizar correctamente.");
    require(result.tokens.size() == expected.size(), "Cantidad de tokens inesperada.");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        require(
            result.tokens[index].type == expected[index],
            "Tipo inesperado en el token " + std::to_string(index) + ".");
    }
}

void test_lexes_keywords_and_identifiers() {
    using lcero::lexer::TokenType;
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize(
        "int if else while print while2 While _x a1",
        diagnostics);

    require_token_types(result, {
        TokenType::KeywordInt,
        TokenType::KeywordIf,
        TokenType::KeywordElse,
        TokenType::KeywordWhile,
        TokenType::KeywordPrint,
        TokenType::Identifier,
        TokenType::Identifier,
        TokenType::Identifier,
        TokenType::Identifier,
        TokenType::EndOfFile
    });
    require(!diagnostics.has_errors(), "Las palabras validas generaron errores.");
}

void test_lexes_numbers_and_unary_minus_separately() {
    using lcero::lexer::TokenType;
    const std::string large_number =
        "12345678901234567890123456789012345678901234567890";
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize("-25 " + large_number, diagnostics);

    require_token_types(result, {
        TokenType::Minus,
        TokenType::Number,
        TokenType::Number,
        TokenType::EndOfFile
    });
    require(result.tokens[1].lexeme == "25", "El signo no debe integrar NUMBER.");
    require(
        result.tokens[2].lexeme == large_number,
        "El literal grande debe conservarse como texto exacto.");
}

void test_lexes_all_operators_and_delimiters() {
    using lcero::lexer::TokenType;
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize(
        "+ - * / == != < > <= >= = ( ) { } ;",
        diagnostics);

    require_token_types(result, {
        TokenType::Plus,
        TokenType::Minus,
        TokenType::Star,
        TokenType::Slash,
        TokenType::EqualEqual,
        TokenType::BangEqual,
        TokenType::Less,
        TokenType::Greater,
        TokenType::LessEqual,
        TokenType::GreaterEqual,
        TokenType::Assign,
        TokenType::LeftParen,
        TokenType::RightParen,
        TokenType::LeftBrace,
        TokenType::RightBrace,
        TokenType::Semicolon,
        TokenType::EndOfFile
    });
}

void test_tracks_lf_and_crlf_positions() {
    using lcero::lexer::TokenType;
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize("int\r\nx\n\t;", diagnostics);

    require_token_types(result, {
        TokenType::KeywordInt,
        TokenType::Identifier,
        TokenType::Semicolon,
        TokenType::EndOfFile
    });
    require(result.tokens[0].line == 1 && result.tokens[0].column == 1,
            "Posicion inicial de int incorrecta.");
    require(result.tokens[1].line == 2 && result.tokens[1].column == 1,
            "CRLF debia contar como un solo salto.");
    require(result.tokens[2].line == 3 && result.tokens[2].column == 2,
            "La tabulacion debe avanzar una columna lexica.");
    require(result.tokens[3].line == 3 && result.tokens[3].column == 3,
            "Posicion de EOF incorrecta.");
}

void test_lexes_multiple_and_nested_statements() {
    using lcero::lexer::TokenType;
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize(
        "int x; x=0; while(x<2){if(x==1){print(x);}x=x+1;}",
        diagnostics);

    require(result.succeeded, "El ejemplo completo debia ser lexicamente valido.");
    require(!diagnostics.has_errors(), "El ejemplo completo genero errores.");
    require(result.tokens.front().type == TokenType::KeywordInt,
            "El primer token debia ser int.");
    require(result.tokens.back().type == TokenType::EndOfFile,
            "El ultimo token debia ser EOF.");
    require(result.tokens.size() == 35, "Cantidad inesperada en el ejemplo anidado.");
}

void test_empty_source_produces_only_eof() {
    using lcero::lexer::TokenType;
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize("", diagnostics);

    require_token_types(result, {TokenType::EndOfFile});
    require(result.tokens[0].line == 1 && result.tokens[0].column == 1,
            "EOF vacio debe ubicarse en 1:1.");
}

void test_does_not_treat_slashes_as_comments() {
    using lcero::lexer::TokenType;
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize("//", diagnostics);
    require_token_types(result, {
        TokenType::Slash,
        TokenType::Slash,
        TokenType::EndOfFile
    });
}

void test_reports_invalid_character_position() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize("int x;\n  @", diagnostics);

    require(!result.succeeded, "@ debia producir un error lexico.");
    require(diagnostics.has_errors(), "Falto diagnostico para @.");
    const auto& diagnostic = diagnostics.diagnostics().back();
    require(
        diagnostic.category == lcero::common::DiagnosticCategory::Lexical,
        "La categoria debia ser lexica.");
    require(diagnostic.location.has_value(), "El error debe tener ubicacion.");
    require(
        diagnostic.location->line == 2 && diagnostic.location->column == 3,
        "La posicion de @ debia ser 2:3.");
}

void test_reports_isolated_bang() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize("x ! 5;", diagnostics);

    require(!result.succeeded, "! aislado debia fallar.");
    require(diagnostics.has_errors(), "Falto diagnostico para ! aislado.");
    require(
        diagnostics.diagnostics().back().location->column == 3,
        "La posicion de ! debia ser la columna 3.");
}

void test_reports_disallowed_operators_and_decimal_point() {
    for (const std::string source : {"x && y", "x || y", "12.3"}) {
        lcero::common::DiagnosticBag diagnostics;
        const auto result = tokenize(source, diagnostics);
        require(!result.succeeded, "La secuencia no admitida debia fallar: " + source);
        require(diagnostics.has_errors(), "Falto un diagnostico lexico.");
    }
}

void test_reports_non_ascii_characters() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize("á", diagnostics);

    require(!result.succeeded, "Un caracter acentuado debia fallar.");
    require(diagnostics.has_errors(), "Falto diagnostico no ASCII.");
    require(
        diagnostics.diagnostics().back().location->line == 1 &&
        diagnostics.diagnostics().back().location->column == 1,
        "El error no ASCII debia ubicarse en 1:1.");
}

void test_maximal_munch() {
    using lcero::lexer::TokenType;
    lcero::common::DiagnosticBag diagnostics;
    const auto result = tokenize("== = <= < >= > != while2", diagnostics);

    require_token_types(result, {
        TokenType::EqualEqual,
        TokenType::Assign,
        TokenType::LessEqual,
        TokenType::Less,
        TokenType::GreaterEqual,
        TokenType::Greater,
        TokenType::BangEqual,
        TokenType::Identifier,
        TokenType::EndOfFile
    });
}

void test_dfa_is_deterministic_complete_and_reachable() {
    const lcero::lexer::LexerAutomata automata;
    const auto& dfa = automata.determinized_dfa();
    const auto& minimized = automata.minimized_dfa();

    require(dfa.is_complete_and_deterministic(), "El AFD debe ser total y determinista.");
    require(minimized.is_complete_and_deterministic(),
            "El AFD minimo debe ser total y determinista.");
    require(dfa.all_states_reachable(), "El AFD conserva un estado inalcanzable.");
    require(minimized.all_states_reachable(),
            "El AFD minimo conserva un estado inalcanzable.");
    require(automata.nfa().state_count() == 67, "El AFN combinado debe tener 67 estados.");
    require(minimized.state_count() <= dfa.state_count(),
            "La minimizacion no puede aumentar estados.");
}

void test_accepting_state_classification() {
    using lcero::lexer::TokenType;
    const lcero::lexer::LexerAutomata automata;
    const auto& dfa = automata.minimized_dfa();

    const std::vector<std::pair<std::string, TokenType>> cases = {
        {"int", TokenType::KeywordInt},
        {"if", TokenType::KeywordIf},
        {"else", TokenType::KeywordElse},
        {"while", TokenType::KeywordWhile},
        {"print", TokenType::KeywordPrint},
        {"while2", TokenType::Identifier},
        {"_a9", TokenType::Identifier},
        {"123", TokenType::Number},
        {"!=", TokenType::BangEqual},
        {"=", TokenType::Assign},
        {"\r\n", TokenType::Whitespace}
    };

    for (const auto& item : cases) {
        require(
            dfa.recognize(item.first) == std::optional<TokenType>(item.second),
            "Clasificacion incorrecta para: " + item.first);
    }
    require(!dfa.recognize("!").has_value(), "! aislado no debe aceptar.");
}

void test_transition_matrix_is_consistent() {
    const lcero::lexer::LexerAutomata automata;
    const auto& dfa = automata.minimized_dfa();
    for (lcero::lexer::StateId state = 0; state < dfa.state_count(); ++state) {
        for (const unsigned char symbol : lcero::lexer::lexical_alphabet()) {
            const auto target = dfa.transition(state, symbol);
            require(target.has_value(), "La matriz tiene una celda indefinida.");
            require(target.value() < dfa.state_count(), "La matriz apunta fuera del AFD.");
        }
    }
}

void test_minimization_preserves_observable_language() {
    const lcero::lexer::LexerAutomata automata;
    require(
        automata.determinized_dfa().equivalent_to(automata.minimized_dfa()),
        "El AFD minimizado cambio lenguaje o clasificacion.");
}

void test_minimized_dfa_has_no_mergeable_states() {
    const lcero::lexer::LexerAutomata automata;
    require(
        !automata.minimized_dfa().has_equivalent_states(),
        "El resultado conserva estados observacionalmente equivalentes.");
}

void generate_and_compare(
    const lcero::lexer::Nfa& nfa,
    const lcero::lexer::Dfa& dfa,
    const lcero::lexer::Dfa& minimized,
    const std::string& symbols,
    std::string& current,
    const std::size_t maximum_length) {
    require(nfa.recognize(current) == dfa.recognize(current),
            "AFN y AFD discrepan para: " + current);
    require(dfa.recognize(current) == minimized.recognize(current),
            "AFD y minimo discrepan para: " + current);

    if (current.size() == maximum_length) {
        return;
    }
    for (const char symbol : symbols) {
        current.push_back(symbol);
        generate_and_compare(nfa, dfa, minimized, symbols, current, maximum_length);
        current.pop_back();
    }
}

void test_nfa_and_dfas_agree_on_generated_strings() {
    const lcero::lexer::LexerAutomata automata;
    std::string current;
    generate_and_compare(
        automata.nfa(),
        automata.determinized_dfa(),
        automata.minimized_dfa(),
        "ai0_=!<+ ;",
        current,
        4);
}

void test_parser_accepts_representative_programs() {
    const std::vector<std::string> programs = {
        "print(1);",
        "int x; int y; x = -1 + 2 * (3 + 4); print(x);",
        "x = 8 / 2;",
        "while (1 < 2) { if (3 >= 2) { print(3); } else { print(2); } }",
        "int x; x = 999999999999999999999999999999999999999999;"
    };

    for (const auto& source : programs) {
        lcero::common::DiagnosticBag diagnostics;
        const auto result = parse_text(source, diagnostics);
        require(result.succeeded && result.program != nullptr,
                "El parser rechazo un programa valido: " + source);
        require(!diagnostics.has_errors(),
                "Un programa valido produjo diagnosticos de error.");
    }
}

void test_parser_accepts_every_comparator() {
    const std::vector<std::pair<std::string, lcero::ast::ComparisonOperator>> comparators = {
        {"==", lcero::ast::ComparisonOperator::Equal},
        {"!=", lcero::ast::ComparisonOperator::NotEqual},
        {"<", lcero::ast::ComparisonOperator::Less},
        {">", lcero::ast::ComparisonOperator::Greater},
        {"<=", lcero::ast::ComparisonOperator::LessEqual},
        {">=", lcero::ast::ComparisonOperator::GreaterEqual}
    };
    for (const auto& item : comparators) {
        lcero::common::DiagnosticBag diagnostics;
        const auto result = parse_text(
            "if (1 " + item.first + " 2) { print(1); }",
            diagnostics);
        require(result.succeeded,
                "El parser rechazo el comparador " + item.first + ".");
        const auto* branch = dynamic_cast<const lcero::ast::IfStatement*>(
            result.program->statements.front().get());
        require(branch != nullptr && branch->condition->operation == item.second,
                "El AST asigno incorrectamente el comparador " + item.first + ".");
    }
}

void test_parser_rejects_invalid_programs() {
    const std::vector<std::string> programs = {
        "",
        "int x;",
        "print(1)",
        "print();",
        "print(1 + );",
        "print(1 + * 2);",
        "int x = 1; print(x);",
        "print(1); int x;",
        "if (1) { print(1); }",
        "if (1 < 2) print(1);",
        "if (1 < 2) { print(1);",
        "while (1 < 2 { print(1); }",
        "while (1 < 2) {}",
        "else { print(1); }",
        "x = 1 print(x);",
        "print(1); )"
    };

    for (const auto& source : programs) {
        lcero::common::DiagnosticBag diagnostics;
        const auto result = parse_text(source, diagnostics);
        require(!result.succeeded && result.program == nullptr,
                "El parser acepto un programa invalido: " + source);
        require(diagnostics.has_errors(),
                "Falto diagnostico para el programa invalido.");
        require(diagnostics.diagnostics().size() == 1,
                "El parser debe detenerse tras el primer error sintactico.");
        require(diagnostics.diagnostics().front().category ==
                    lcero::common::DiagnosticCategory::Syntactic,
                "El error del parser debe usar la categoria sintactica.");
    }
}

void test_parser_reports_expected_token_and_position() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = parse_text("print(1)", diagnostics);

    require(!result.succeeded, "Debia faltar el punto y coma.");
    const auto& diagnostic = diagnostics.diagnostics().front();
    require(diagnostic.message.find("SEMICOLON") != std::string::npos,
            "El diagnostico no informa el terminal esperado.");
    require(diagnostic.location.has_value(), "El error sintactico no tiene ubicacion.");
    require(diagnostic.location->line == 1 && diagnostic.location->column == 9,
            "La ubicacion del EOF inesperado es incorrecta.");
}

void test_ast_preserves_precedence_and_associativity() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = parse_text("print(10 - 3 - 2 * 4);", diagnostics);
    require(result.succeeded, "No se pudo construir el AST aritmetico.");

    const auto* print = dynamic_cast<const lcero::ast::PrintStatement*>(
        result.program->statements.front().get());
    require(print != nullptr, "La raiz de la instruccion debia ser impresion.");
    const auto* outer_subtract = dynamic_cast<const lcero::ast::BinaryExpression*>(
        print->expression.get());
    require(outer_subtract != nullptr &&
                outer_subtract->operation == lcero::ast::BinaryOperator::Subtract,
            "La resta exterior no fue representada.");
    const auto* inner_subtract = dynamic_cast<const lcero::ast::BinaryExpression*>(
        outer_subtract->left.get());
    require(inner_subtract != nullptr &&
                inner_subtract->operation == lcero::ast::BinaryOperator::Subtract,
            "La resta debe asociar a izquierda.");
    const auto* multiplication = dynamic_cast<const lcero::ast::BinaryExpression*>(
        outer_subtract->right.get());
    require(multiplication != nullptr &&
                multiplication->operation == lcero::ast::BinaryOperator::Multiply,
            "La multiplicacion debe tener mayor precedencia que la resta.");
}

void test_ast_represents_unary_parentheses_and_large_literals() {
    const std::string large = "1234567890123456789012345678901234567890";
    lcero::common::DiagnosticBag diagnostics;
    const auto result = parse_text(
        "int x;\nprint(-(x + " + large + "));",
        diagnostics);
    require(result.succeeded, "No se pudo construir el AST unario.");
    require(result.program->declarations.front().name == "x",
            "La declaracion perdio el identificador.");
    require(result.program->declarations.front().position.line == 1 &&
                result.program->declarations.front().position.column == 5,
            "La declaracion perdio su posicion de origen.");

    const auto* print = dynamic_cast<const lcero::ast::PrintStatement*>(
        result.program->statements.front().get());
    const auto* unary = dynamic_cast<const lcero::ast::UnaryExpression*>(
        print->expression.get());
    require(unary != nullptr && unary->operation == lcero::ast::UnaryOperator::Negate,
            "El menos unario no fue representado.");
    const auto* addition = dynamic_cast<const lcero::ast::BinaryExpression*>(
        unary->operand.get());
    require(addition != nullptr && addition->operation == lcero::ast::BinaryOperator::Add,
            "Los parentesis no preservaron la suma bajo el unario.");
    const auto* literal = dynamic_cast<const lcero::ast::IntegerLiteral*>(
        addition->right.get());
    require(literal != nullptr && literal->value == large,
            "El literal grande no se conservo como texto decimal.");
}

void test_ast_represents_control_flow_and_order() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = parse_text(
        "int x; x = 1; if (x != 0) { print(x); } else { while (x > 0) { x = x - 1; } } print(x);",
        diagnostics);
    require(result.succeeded, "No se pudo construir el AST de control.");
    require(result.program->statements.size() == 3,
            "El AST no preservo el orden de instrucciones superiores.");

    const auto* branch = dynamic_cast<const lcero::ast::IfStatement*>(
        result.program->statements[1].get());
    require(branch != nullptr && branch->else_block != nullptr,
            "El condicional o su else no fueron representados.");
    require(branch->condition->operation == lcero::ast::ComparisonOperator::NotEqual,
            "El comparador del condicional es incorrecto.");
    const auto* loop = dynamic_cast<const lcero::ast::WhileStatement*>(
        branch->else_block->statements.front().get());
    require(loop != nullptr && loop->body->statements.size() == 1,
            "El bucle anidado no fue representado.");
}

void test_grammar_inventory_and_nullable_set() {
    using lcero::parser::Nonterminal;
    const lcero::parser::GrammarAnalysis analysis;
    require(analysis.grammar().nonterminals().size() == 20,
            "Cantidad de no terminales inesperada.");
    require(analysis.grammar().terminals().size() == 24,
            "Cantidad de terminales inesperada.");
    require(analysis.grammar().productions().size() == 38,
            "Cantidad de producciones inesperada.");

    const std::set<Nonterminal> expected = {
        Nonterminal::DeclarationSequence,
        Nonterminal::StatementSequenceTail,
        Nonterminal::ElseOptional,
        Nonterminal::ExpressionTail,
        Nonterminal::TermTail
    };
    std::set<Nonterminal> actual;
    for (const auto nonterminal : analysis.grammar().nonterminals()) {
        if (analysis.nullable(nonterminal)) {
            actual.insert(nonterminal);
        }
    }
    require(actual == expected, "El conjunto NULLABLE calculado es incorrecto.");
    require(analysis.nullable_pass_count() > 1 &&
                analysis.first_pass_count() > 1 &&
                analysis.follow_pass_count() > 1,
            "Los calculos formales no recorrieron hasta punto fijo.");
}

void test_first_and_follow_sets() {
    using lcero::lexer::TokenType;
    using lcero::parser::Nonterminal;
    const lcero::parser::GrammarAnalysis analysis;

    require(analysis.first(Nonterminal::Program) == std::set<TokenType>{
                TokenType::KeywordInt, TokenType::KeywordIf,
                TokenType::KeywordWhile, TokenType::KeywordPrint,
                TokenType::Identifier},
            "FIRST(Programa) es incorrecto.");
    require(analysis.first(Nonterminal::Expression) == std::set<TokenType>{
                TokenType::Identifier, TokenType::Number, TokenType::Minus,
                TokenType::LeftParen},
            "FIRST(Expresion) es incorrecto.");
    require(analysis.follow(Nonterminal::StatementSequence) == std::set<TokenType>{
                TokenType::RightBrace, TokenType::EndOfFile},
            "FOLLOW(StmtSeq) es incorrecto.");
    require(analysis.follow(Nonterminal::Expression) == std::set<TokenType>{
                TokenType::EqualEqual, TokenType::BangEqual, TokenType::Less,
                TokenType::Greater, TokenType::LessEqual, TokenType::GreaterEqual,
                TokenType::RightParen, TokenType::Semicolon},
            "FOLLOW(Expresion) es incorrecto.");
}

void test_select_sets_and_parse_table_are_ll1() {
    const lcero::parser::GrammarAnalysis analysis;
    const auto& productions = analysis.grammar().productions();
    require(analysis.is_ll1() && analysis.conflicts().empty(),
            "La gramatica debe ser LL(1) sin conflictos.");

    for (std::size_t left_index = 0; left_index < productions.size(); ++left_index) {
        const auto& left = productions[left_index];
        require(!analysis.select(left.number).empty(),
                "Una produccion tiene SELECT vacio.");
        for (const auto token : analysis.select(left.number)) {
            require(analysis.production_for(left.left, token) == left.number,
                    "La tabla predictiva no coincide con SELECT.");
        }
        for (std::size_t right_index = left_index + 1;
             right_index < productions.size(); ++right_index) {
            const auto& right = productions[right_index];
            if (left.left != right.left) {
                continue;
            }
            std::vector<lcero::lexer::TokenType> intersection;
            std::set_intersection(
                analysis.select(left.number).begin(), analysis.select(left.number).end(),
                analysis.select(right.number).begin(), analysis.select(right.number).end(),
                std::back_inserter(intersection));
            require(intersection.empty(),
                    "Dos alternativas de un no terminal comparten SELECT.");
        }
    }
}

void test_symbol_table_tracks_global_declarations() {
    lcero::semantic::SymbolTable table;
    const lcero::ast::Declaration first({1, 5, 4}, "x");
    const lcero::ast::Declaration duplicate({2, 5, 11}, "x");
    const lcero::ast::Declaration second({3, 5, 18}, "y");

    const auto first_result = table.insert(first);
    const auto duplicate_result = table.insert(duplicate);
    const auto second_result = table.insert(second);
    require(first_result.inserted && second_result.inserted,
            "La tabla rechazo una declaracion nueva.");
    require(!duplicate_result.inserted,
            "La tabla acepto una declaracion duplicada.");
    require(duplicate_result.symbol != nullptr &&
                duplicate_result.symbol->declaration_position.line == 1,
            "La tabla no conservo la ubicacion de la declaracion original.");
    require(table.size() == 2 && table.contains("x") && table.contains("y"),
            "El inventario de simbolos es incorrecto.");
    require(table.find("x")->type == lcero::semantic::SymbolType::Integer,
            "El simbolo no conserva el tipo entero.");
    require(table.declared_names() == std::set<std::string>{"x", "y"},
            "D no coincide con las claves de la tabla.");
}

void test_semantic_accepts_representative_programs() {
    const std::vector<std::string> programs = {
        "print(5);",
        "int x; x = 5; print(x);",
        "int x; int y; x = 2; y = x * (x + 1); print(y);",
        "int x; x = -(2 + 3); print(x);",
        "int x; if (5 > 3) { x = 10; } else { x = 20; } print(x);",
        "int x; x = 0; if (x == 0) { print(x); } print(x);",
        "int x; x = 0; while (x < 5) { print(x); x = x + 1; } print(x);",
        "int x; x = 0; while (x < 5) { if (x == 2) { print(x); } else { x = x + 1; } }",
        "int x; x = 0; while (x < 2) { while (x < 1) { x = x + 1; } print(x); }",
        "int x; x = 1; if (x > 0) { if (x == 1) { print(x); } else { print(0); } } else { print(0); }",
        "int unused; print(1);",
        "print(10 / 0);",
        "int divisor; divisor = 0; print(10 / divisor);",
        "int huge; huge = 999999999999999999999999999999999999999999999999; print(huge);"
    };

    for (const auto& source : programs) {
        lcero::common::DiagnosticBag diagnostics;
        const auto result = analyze_text(source, diagnostics);
        require(result.succeeded,
                "El analizador rechazo un programa semanticamente valido: " + source);
        require(!diagnostics.has_errors(),
                "Un programa semanticamente valido produjo errores.");
    }
}

void test_semantic_reports_duplicate_with_previous_location() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = analyze_text(
        "int x;\nint x;\nprint(5);",
        diagnostics);
    require(!result.succeeded, "La declaracion duplicada debia rechazarse.");
    require(diagnostics.diagnostics().size() == 1,
            "Debia emitirse un solo error por duplicacion.");
    const auto& diagnostic = diagnostics.diagnostics().front();
    require(diagnostic.category == lcero::common::DiagnosticCategory::Semantic,
            "La duplicacion debe ser un error semantico.");
    require(diagnostic.message.find("linea 1, columna 5") != std::string::npos,
            "El mensaje no informa la declaracion anterior.");
    require(diagnostic.location.has_value() &&
                diagnostic.location->line == 2 && diagnostic.location->column == 5,
            "El error no apunta a la segunda declaracion.");
}

void test_semantic_rejects_undeclared_variables() {
    require_semantic_error("x = 5;", "Variable no declarada: 'x'");
    require_semantic_error("int x; x = y + 1;", "Variable no declarada: 'y'");
    require_semantic_error(
        "int x; x = 1; print(x + z);",
        "Variable no declarada: 'z'");
}

void test_semantic_rejects_uninitialized_reads() {
    require_semantic_error("int x; print(x);", "Variable no inicializada: 'x'");
    require_semantic_error(
        "int x; if (x > 0) { print(5); }",
        "Variable no inicializada: 'x'");
    require_semantic_error(
        "int x; int y; x = 1; print(x + y);",
        "Variable no inicializada: 'y'");
    require_semantic_error("int x; x = x + 1;", "Variable no inicializada: 'x'");
}

void test_semantic_if_is_conservative() {
    require_semantic_error(
        "int x; if (1 == 1) { x = 10; } print(x);",
        "Variable no inicializada: 'x'");
    require_semantic_error(
        "int x; if (1 == 1) { x = 10; } else { print(0); } print(x);",
        "Variable no inicializada: 'x'");

    lcero::common::DiagnosticBag diagnostics;
    const auto result = analyze_text(
        "int x; if (1 == 1) { x = 10; } else { x = 20; } print(x);",
        diagnostics);
    require(result.succeeded && result.definitely_initialized.count("x") == 1,
            "La interseccion de dos ramas que inicializan x debia conservarla.");
}

void test_semantic_while_does_not_propagate_initialization() {
    require_semantic_error(
        "int x; while (1 == 0) { x = 5; } print(x);",
        "Variable no inicializada: 'x'");

    lcero::common::DiagnosticBag diagnostics;
    const auto result = analyze_text(
        "int x; x = 0; while (x < 2) { x = x + 1; } print(x);",
        diagnostics);
    require(result.succeeded && result.definitely_initialized ==
                std::set<std::string>{"x"},
            "El while debia preservar la inicializacion de entrada.");
}

void test_semantic_finds_errors_inside_nested_structures() {
    require_semantic_error(
        "int x; x = 0; while (x < 2) { if (x == 1) { print(y); } else { print(x); } }",
        "Variable no declarada: 'y'");
    require_semantic_error(
        "int x; while (1 < 2) { while (1 < 2) { print(x); } }",
        "Variable no inicializada: 'x'");
}

void test_semantic_respects_order_and_global_visibility() {
    require_semantic_error(
        "int x; print(x); x = 1;",
        "Variable no inicializada: 'x'");

    lcero::common::DiagnosticBag diagnostics;
    const auto result = analyze_text(
        "int x; x = 1; if (x > 0) { print(x); } else { while (x > 0) { print(x); } }",
        diagnostics);
    require(result.succeeded,
            "Una declaracion global inicializada debe ser visible en todos los bloques.");
}

void test_semantic_transfer_sets_and_invariant() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = analyze_text(
        "int x; int y; int z; "
        "x = 1; "
        "if (x > 0) { y = 2; } else { y = 3; z = 4; } "
        "while (x > 0) { z = 5; x = x - 1; } "
        "print(y);",
        diagnostics);
    require(result.succeeded, "El programa de transferencias debia ser valido.");

    const auto declared = result.symbol_table.declared_names();
    for (const auto& transfer : result.transfers) {
        require(std::includes(
                    declared.begin(), declared.end(),
                    transfer.input.begin(), transfer.input.end()) &&
                std::includes(
                    declared.begin(), declared.end(),
                    transfer.output.begin(), transfer.output.end()) &&
                std::includes(
                    declared.begin(), declared.end(),
                    transfer.then_output.begin(), transfer.then_output.end()) &&
                std::includes(
                    declared.begin(), declared.end(),
                    transfer.else_output.begin(), transfer.else_output.end()),
                "Una transferencia violo I subconjunto D.");
    }

    const lcero::semantic::TransferRecord* branch = nullptr;
    const lcero::semantic::TransferRecord* loop = nullptr;
    for (const auto& transfer : result.transfers) {
        if (transfer.kind == lcero::semantic::TransferKind::IfWithElse) {
            branch = &transfer;
        } else if (transfer.kind == lcero::semantic::TransferKind::While) {
            loop = &transfer;
        }
    }
    require(branch != nullptr &&
                branch->then_output == std::set<std::string>{"x", "y"} &&
                branch->else_output == std::set<std::string>{"x", "y", "z"} &&
                branch->output == std::set<std::string>{"x", "y"},
            "La salida del if/else no coincide con la interseccion de ramas.");
    require(loop != nullptr && loop->input == loop->output &&
                loop->then_output == std::set<std::string>{"x", "y", "z"},
            "El while no preservo su entrada o no analizo el cuerpo.");
    require(result.definitely_initialized == std::set<std::string>{"x", "y"},
            "El estado final conservador es incorrecto.");
}

void test_compiler_stops_on_semantic_error() {
    TemporaryDirectory temporary;
    const auto source_path = temporary.path() / "Source/invalido_semantico.lc";
    const auto output_path = temporary.path() / "LCIs/invalido_semantico.lci";
    write_text(source_path, "int x; print(x);\n");
    write_text(output_path, "salida anterior");

    lcero::common::DiagnosticBag diagnostics;
    const lcero::Compiler compiler;
    const auto result = compiler.run(
        lcero::CompilerOptions{source_path, output_path},
        diagnostics);

    require(result.status == lcero::CompilationStatus::Failed,
            "Un error semantico debe fallar la compilacion coordinada.");
    require(result.lexical_analysis_completed && result.syntactic_analysis_completed,
            "Lexico y sintaxis debian completarse antes del error semantico.");
    require(!result.semantic_analysis_completed,
            "Semantica no debe marcarse completa tras un error.");
    require(diagnostics.diagnostics().back().category ==
                lcero::common::DiagnosticCategory::Semantic,
            "El coordinador no conservo la categoria semantica.");
    require(!std::filesystem::exists(output_path),
            "Un error semantico no debe conservar ni crear un .lci.");

    write_text(source_path, "int x; x = 5; print(x);\n");
    lcero::common::DiagnosticBag valid_diagnostics;
    const auto valid_result = compiler.run(
        lcero::CompilerOptions{source_path, output_path},
        valid_diagnostics);
    require(valid_result.status == lcero::CompilationStatus::Succeeded &&
                valid_result.semantic_analysis_completed &&
                valid_result.ir_generation_completed &&
                valid_result.lci_emission_completed,
            "Un programa semanticamente valido debia completar el pipeline.");
    require(valid_result.symbol_count == 1 &&
                valid_result.initialized_variable_count == 1,
            "El resumen semantico integrado es incorrecto.");
    require(!valid_diagnostics.has_errors() && std::filesystem::exists(output_path),
            "El pipeline valido debia producir un .lci.");
}

void test_ir_preserves_expression_structure_and_variable_order() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = generate_ir(
        "int z; int a; z = 10 - 3 - 2 * 4; a = -z; print(a);",
        diagnostics);

    require(result.succeeded && !diagnostics.has_errors(),
            "La traduccion de expresiones a RI debia ser valida.");
    require(result.program.variables.size() == 2 &&
                result.program.variables[0].name == "z" &&
                result.program.variables[0].address == 0 &&
                result.program.variables[1].name == "a" &&
                result.program.variables[1].address == 1,
            "Las direcciones no respetan el orden de declaracion.");

    std::vector<lcero::ir::Opcode> operations;
    for (const auto& record : result.program.instructions) {
        operations.push_back(lcero::ir::opcode_of(record.instruction));
    }
    const std::vector<lcero::ir::Opcode> expected = {
        lcero::ir::Opcode::Constant,
        lcero::ir::Opcode::Constant,
        lcero::ir::Opcode::Subtract,
        lcero::ir::Opcode::Constant,
        lcero::ir::Opcode::Constant,
        lcero::ir::Opcode::Multiply,
        lcero::ir::Opcode::Subtract,
        lcero::ir::Opcode::StoreVariable,
        lcero::ir::Opcode::Constant,
        lcero::ir::Opcode::LoadVariable,
        lcero::ir::Opcode::Subtract,
        lcero::ir::Opcode::StoreVariable,
        lcero::ir::Opcode::LoadVariable,
        lcero::ir::Opcode::Print,
        lcero::ir::Opcode::Halt
    };
    require(operations == expected,
            "La RI no preservo precedencia, asociatividad u orden de sentencias.");
    require(result.program.temporary_count == 11,
            "Cada resultado de expresion debe tener un temporal virtual unico.");
}

void test_ir_canonicalizes_large_literals_without_numeric_conversion() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = generate_ir(
        "print(0001234567890123456789012345678901234567890);",
        diagnostics);
    require(result.succeeded, "El literal grande debia traducirse.");
    const auto* constant = std::get_if<lcero::ir::ConstantInstruction>(
        &result.program.instructions.front().instruction);
    require(constant != nullptr &&
                constant->decimal_value ==
                    "1234567890123456789012345678901234567890",
            "El literal grande no se conservo como decimal canonico exacto.");
}

void test_ir_maps_every_comparison_and_control_flow() {
    const std::vector<std::pair<std::string, lcero::ir::Opcode>> cases = {
        {"==", lcero::ir::Opcode::CompareEqual},
        {"!=", lcero::ir::Opcode::CompareNotEqual},
        {"<", lcero::ir::Opcode::CompareLess},
        {"<=", lcero::ir::Opcode::CompareLessEqual},
        {">", lcero::ir::Opcode::CompareGreater},
        {">=", lcero::ir::Opcode::CompareGreaterEqual}
    };
    for (const auto& item : cases) {
        lcero::common::DiagnosticBag diagnostics;
        const auto result = generate_ir(
            "if (1 " + item.first + " 2) { print(3); }",
            diagnostics);
        bool found = false;
        for (const auto& record : result.program.instructions) {
            const auto* comparison =
                std::get_if<lcero::ir::ComparisonInstruction>(&record.instruction);
            found = found ||
                (comparison != nullptr && comparison->operation == item.second);
        }
        require(result.succeeded && found,
                "Un comparador no se mapeo a su opcode de RI.");
    }

    lcero::common::DiagnosticBag diagnostics;
    const auto nested = generate_ir(
        "int x; x = 0; while (x < 3) { if (x == 1) { print(x); } "
        "else { print(0); } x = x + 1; }",
        diagnostics);
    require(nested.succeeded && nested.program.label_count == 4,
            "El control anidado debia crear cuatro etiquetas unicas.");
    std::set<std::size_t> definitions;
    std::set<std::size_t> references;
    for (const auto& record : nested.program.instructions) {
        if (const auto* label =
                std::get_if<lcero::ir::LabelInstruction>(&record.instruction)) {
            require(definitions.insert(label->label.id).second,
                    "Una etiqueta de RI fue definida dos veces.");
        } else if (const auto* jump =
                       std::get_if<lcero::ir::JumpInstruction>(&record.instruction)) {
            references.insert(jump->target.id);
        } else if (const auto* jump_zero =
                       std::get_if<lcero::ir::JumpIfZeroInstruction>(
                           &record.instruction)) {
            references.insert(jump_zero->target.id);
        }
    }
    require(definitions == references && definitions.size() == 4,
            "Las referencias de control no coinciden con las etiquetas definidas.");
}

void test_reference_backend_uses_only_contract_resources() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = emit_lci(
        "int x; x = (8 + 4) * 2 / 3; print(x);",
        diagnostics);
    require(result.succeeded && !diagnostics.has_errors(),
            "El backend de referencia debia emitir el programa.");
    require(result.text.find("R3") == std::string::npos &&
                result.text.find(" R4") == std::string::npos,
            "El backend invento un registro fuera de su esquema conservador.");
    require(result.text.rfind("LCERO_IR 1\nMEM ", 0) == 0,
            "Faltan las directivas iniciales exactas.");
    const lcero::ir::LciValidator validator;
    require(validator.validate(result.text, "prueba.lci", diagnostics),
            "La salida del backend no cumple LCERO_IR 1.");
}

void test_lci_validator_accepts_contract_and_rejects_malformed_text() {
    const std::string valid =
        "LCERO_IR 1\nMEM 2\n"
        "CONST R0, -7\nCONST R1, 2\nMOV R3, R0\n"
        "ADD R2, R0, R1\nSUB R2, R0, R1\nMUL R2, R0, R1\n"
        "DIV R2, R0, R1\nCMP_EQ R2, R0, R1\nCMP_NE R2, R0, R1\n"
        "CMP_LT R2, R0, R1\nCMP_LE R2, R0, R1\n"
        "CMP_GT R2, R0, R1\nCMP_GE R2, R0, R1\n"
        "STORE 0, R2\nLOAD R0, 0\nJZ R0, fin\nJMP inicio\n"
        "LABEL inicio\nPRINT R0\nLABEL fin\nHALT\n";
    lcero::common::DiagnosticBag valid_diagnostics;
    const lcero::ir::LciValidator validator;
    require(validator.validate(valid, "valido.lci", valid_diagnostics) &&
                !valid_diagnostics.has_errors(),
            "El validador rechazo instrucciones autorizadas por el contrato.");

    const std::vector<std::string> invalid = {
        "LCERO_IR 2\nMEM 0\nHALT\n",
        "LCERO_IR 1\nMEM 01\nHALT\n",
        "LCERO_IR 1\nMEM 0\nCONST R0, -0\nHALT\n",
        "LCERO_IR 1\nMEM 0\nCONST R4, 1\nHALT\n",
        "LCERO_IR 1\nMEM 1\nLOAD R0, 1\nHALT\n",
        "LCERO_IR 1\nMEM 0\nLABEL x\nLABEL x\nHALT\n",
        "LCERO_IR 1\nMEM 0\nJMP falta\nHALT\n",
        "LCERO_IR 1\nMEM 0\nNEG R0\nHALT\n",
        "LCERO_IR 1\nMEM 0\nPRINT R0\n",
        "LCERO_IR 1\nMEM 0\nHALT # comentario\n"
    };
    for (const auto& text : invalid) {
        lcero::common::DiagnosticBag diagnostics;
        require(!validator.validate(text, "invalido.lci", diagnostics) &&
                    diagnostics.has_errors(),
                "El validador acepto una extension o forma invalida del contrato.");
    }
}

void test_reference_backend_rejects_inconsistent_internal_ir() {
    TemporaryDirectory temporary;
    const auto output_path = temporary.path() / "salida.lci";
    write_text(output_path, "salida anterior");
    const lcero::common::OutputArtifact artifact;
    lcero::ir::Program malformed;
    malformed.instructions.push_back(lcero::ir::InstructionRecord{
        lcero::ir::ConstantInstruction{lcero::ir::VirtualTemporary{0}, "1"},
        {1, 1}
    });
    malformed.instructions.push_back(lcero::ir::InstructionRecord{
        lcero::ir::HaltInstruction{}, {1, 1}
    });
    lcero::common::DiagnosticBag diagnostics;
    require(artifact.invalidate_previous(output_path, diagnostics),
            "No se pudo preparar la prueba de fallo interno.");
    const lcero::ir::ReferenceBackend backend;
    const auto result = backend.emit(malformed, output_path, diagnostics);
    require(!result.succeeded && result.text.empty() && diagnostics.has_errors(),
            "El backend debia rechazar un temporal fuera del universo declarado.");
    lcero::common::DiagnosticBag optimized_diagnostics;
    const lcero::ir::OptimizedBackend optimized_backend;
    const auto optimized_result = optimized_backend.emit(
        malformed, output_path, optimized_diagnostics);
    require(!optimized_result.succeeded && optimized_result.text.empty() &&
                optimized_diagnostics.has_errors(),
            "El backend optimizado no debe recurrir silenciosamente al de referencia.");
    require(!std::filesystem::exists(output_path) &&
                !std::filesystem::exists(
                    lcero::common::OutputArtifact::temporary_path_for(output_path)),
            "Un error interno conservo o publico una salida enganosa.");
}

lcero::ir::Program make_loop_ir_for_analysis() {
    using namespace lcero::ir;
    lcero::ir::Program program;
    program.temporary_count = 4;
    program.label_count = 2;
    auto add = [&](Instruction instruction) {
        program.instructions.push_back(InstructionRecord{
            std::move(instruction), {1, 1}
        });
    };
    add(ConstantInstruction{{0}, "0"});
    add(LabelInstruction{{0}});
    add(ConstantInstruction{{1}, "3"});
    add(ComparisonInstruction{Opcode::CompareLess, {2}, {0}, {1}});
    add(JumpIfZeroInstruction{{2}, {1}});
    add(ConstantInstruction{{3}, "1"});
    add(JumpInstruction{{0}});
    add(LabelInstruction{{1}});
    add(PrintInstruction{{0}});
    add(HaltInstruction{});
    return program;
}

void test_cfg_builds_basic_blocks_edges_and_cycle() {
    const auto program = make_loop_ir_for_analysis();
    lcero::common::DiagnosticBag diagnostics;
    const lcero::ir::ControlFlowGraphBuilder builder;
    const auto result = builder.build(program, "cfg.lc", diagnostics);
    require(result.succeeded && !diagnostics.has_errors(),
            "El CFG del bucle debia construirse.");
    require(result.graph.blocks.size() == 4 && result.graph.edge_count == 4,
            "Los lideres o aristas del CFG son incorrectos.");
    require(result.graph.blocks[0].begin == 0 && result.graph.blocks[0].end == 1 &&
                result.graph.blocks[1].begin == 1 && result.graph.blocks[1].end == 5 &&
                result.graph.blocks[2].begin == 5 && result.graph.blocks[2].end == 7 &&
                result.graph.blocks[3].begin == 7 && result.graph.blocks[3].end == 10,
            "Los limites de bloques basicos no coinciden con los lideres.");
    require(result.graph.blocks[1].successors ==
                std::vector<std::size_t>({2, 3}) &&
                result.graph.blocks[2].successors ==
                    std::vector<std::size_t>({1}),
            "JZ, JMP o el ciclo no tienen los sucesores correctos.");
    require(result.graph.blocks[1].predecessors ==
                std::vector<std::size_t>({0, 2}),
            "Los predecesores del encabezado del bucle son incorrectos.");
}

void test_liveness_reaches_consistent_fixed_point() {
    const auto program = make_loop_ir_for_analysis();
    lcero::common::DiagnosticBag diagnostics;
    const lcero::ir::ControlFlowGraphBuilder cfg_builder;
    auto cfg = cfg_builder.build(program, "vivacidad.lc", diagnostics);
    const lcero::ir::LivenessAnalyzer analyzer;
    const auto result = analyzer.analyze(
        program, std::move(cfg.graph), "vivacidad.lc", diagnostics);
    require(result.succeeded && result.fixed_point_iterations >= 2,
            "La vivacidad del ciclo debia iterar hasta un punto fijo.");
    require(result.graph.blocks[0].definition == lcero::ir::TemporarySet({0}) &&
                result.graph.blocks[0].live_out == lcero::ir::TemporarySet({0}),
            "DEF u OUT del bloque de entrada es incorrecto.");
    require(result.graph.blocks[1].use == lcero::ir::TemporarySet({0}) &&
                result.graph.blocks[1].definition ==
                    lcero::ir::TemporarySet({1, 2}) &&
                result.graph.blocks[1].live_in == lcero::ir::TemporarySet({0}) &&
                result.graph.blocks[1].live_out == lcero::ir::TemporarySet({0}),
            "USE, DEF, IN u OUT del encabezado es incorrecto.");
    require(result.instruction_live_out[3] ==
                lcero::ir::TemporarySet({0, 2}) &&
                result.instruction_live_in[4] ==
                    lcero::ir::TemporarySet({0, 2}),
            "La vivacidad por instruccion no conserva la condicion y el ciclo.");
}

void test_interference_is_symmetric_precise_and_loop_aware() {
    const auto program = make_loop_ir_for_analysis();
    lcero::common::DiagnosticBag diagnostics;
    const lcero::ir::ControlFlowGraphBuilder cfg_builder;
    auto cfg = cfg_builder.build(program, "interferencia.lc", diagnostics);
    const lcero::ir::LivenessAnalyzer analyzer;
    const auto liveness = analyzer.analyze(
        program, std::move(cfg.graph), "interferencia.lc", diagnostics);
    const lcero::ir::InterferenceGraphBuilder builder;
    const auto graph = builder.build(program, liveness);
    require(graph.adjacency.size() == 4,
            "Faltan vertices temporales en el grafo de interferencia.");
    for (std::size_t node = 0; node < graph.adjacency.size(); ++node) {
        require(graph.adjacency[node].count(node) == 0,
                "El grafo contiene una autointerferencia.");
        for (const auto neighbor : graph.adjacency[node]) {
            require(graph.adjacency[neighbor].count(node) == 1,
                    "El grafo de interferencia no es simetrico.");
        }
    }
    require(graph.adjacency[0].count(1) == 1 &&
                graph.adjacency[0].count(2) == 1 &&
                graph.adjacency[1].count(2) == 0,
            "Las interferencias simultaneas del comparador son incorrectas.");
    require(graph.adjacency[3] == lcero::ir::TemporarySet({0}),
            "La definicion del cuerpo debe proteger t0 vivo durante el ciclo sin "
            "interferir indiscriminadamente con otros temporales.");
}

void test_chaitin_colors_and_spills_deterministically() {
    lcero::ir::InterferenceGraph path;
    path.adjacency = {{1}, {0, 2}, {1}};
    path.use_frequency = {2, 3, 2};
    path.edge_count = 2;
    const lcero::ir::ChaitinAllocator allocator;
    const auto colored = allocator.allocate(path, 0, 2);
    require(colored.succeeded && colored.spilled_temporary_count == 0,
            "Un camino debe ser 2-coloreable sin spilling.");
    require(colored.colors[0] != colored.colors[1] &&
                colored.colors[1] != colored.colors[2],
            "Dos vecinos recibieron el mismo color.");

    lcero::ir::InterferenceGraph triangle;
    triangle.adjacency = {{1, 2}, {0, 2}, {0, 1}};
    triangle.use_frequency = {1, 5, 5};
    triangle.edge_count = 3;
    const auto first = allocator.allocate(triangle, 4, 2);
    const auto second = allocator.allocate(triangle, 4, 2);
    require(first.succeeded && first.spilled_temporary_count == 1 &&
                first.colors == second.colors &&
                first.spill_slots == second.spill_slots &&
                first.potential_spill_order == second.potential_spill_order,
            "Chaitin debe derramar el triangulo de forma determinista.");
    require(first.spill_slots[0] == std::optional<std::size_t>{4},
            "La heuristica frecuencia/grado no eligio el candidato esperado.");
}

void test_optimized_backend_emits_coloring_and_real_spills() {
    const std::string source =
        "int x; x = (1 + 2) * (3 + 4); "
        "if (x >= 10) { print(x); } else { print(-x); }";
    lcero::common::DiagnosticBag optimized_diagnostics;
    const auto optimized = emit_optimized_lci(source, optimized_diagnostics);
    lcero::common::DiagnosticBag reference_diagnostics;
    const auto reference = emit_lci(source, reference_diagnostics);
    require(optimized.succeeded && reference.succeeded &&
                optimized.spilled_temporary_count > 0,
            "El programa de presion debia ejercitar spilling real.");
    require(optimized.memory_slot_count ==
                1 + optimized.spilled_temporary_count &&
                optimized.memory_slot_count < reference.memory_slot_count,
            "MEM no contiene exactamente variables mas temporales derramados.");
    require(optimized.text.find("R4") == std::string::npos &&
                optimized.text.find("R2") != std::string::npos &&
                optimized.text.find("R3") != std::string::npos,
            "El coloreo no respeto R2/R3 o invento registros.");
    const lcero::ir::LciValidator validator;
    require(validator.validate(
                optimized.text, "optimizado.lci", optimized_diagnostics),
            "La salida optimizada no cumple LCERO_IR 1.");
    require(optimized.load_count + optimized.store_count <
                reference.load_count + reference.store_count,
            "El caso representativo no redujo accesos a memoria.");
}

struct LciExecutionResult {
    bool halted = false;
    bool dynamic_error = false;
    bool step_limit_reached = false;
    std::vector<std::int64_t> output;
};

LciExecutionResult execute_lci_for_test(
    const std::string& text,
    const std::size_t step_limit = 10000) {
    std::vector<std::vector<std::string>> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream words(line);
        std::vector<std::string> tokens{
            std::istream_iterator<std::string>{words},
            std::istream_iterator<std::string>{}
        };
        if (!tokens.empty()) {
            lines.push_back(std::move(tokens));
        }
    }
    require(lines.size() >= 3 && lines[0] == std::vector<std::string>({"LCERO_IR", "1"}) &&
                lines[1].size() == 2 && lines[1][0] == "MEM",
            "El ejecutor acotado recibio un archivo invalido.");
    std::vector<std::int64_t> memory(
        static_cast<std::size_t>(std::stoull(lines[1][1])));
    std::array<std::int64_t, 4> registers{};
    std::map<std::string, std::size_t> labels;
    for (std::size_t index = 2; index < lines.size(); ++index) {
        if (lines[index][0] == "LABEL") {
            labels[lines[index][1]] = index;
        }
    }
    auto reg = [](const std::string& name) {
        return static_cast<std::size_t>(name[1] - '0');
    };

    LciExecutionResult result;
    std::size_t pc = 2;
    std::size_t steps = 0;
    while (pc < lines.size()) {
        const auto& tokens = lines[pc];
        const auto& op = tokens[0];
        if (op == "LABEL") {
            ++pc;
            continue;
        }
        if (steps++ >= step_limit) {
            result.step_limit_reached = true;
            return result;
        }
        if (op == "CONST") {
            registers[reg(tokens[1])] = std::stoll(tokens[2]);
        } else if (op == "LOAD") {
            registers[reg(tokens[1])] = memory[std::stoull(tokens[2])];
        } else if (op == "STORE") {
            memory[std::stoull(tokens[1])] = registers[reg(tokens[2])];
        } else if (op == "MOV") {
            registers[reg(tokens[1])] = registers[reg(tokens[2])];
        } else if (op == "JMP") {
            pc = labels.at(tokens[1]);
            continue;
        } else if (op == "JZ") {
            if (registers[reg(tokens[1])] == 0) {
                pc = labels.at(tokens[2]);
                continue;
            }
        } else if (op == "PRINT") {
            result.output.push_back(registers[reg(tokens[1])]);
        } else if (op == "HALT") {
            result.halted = true;
            return result;
        } else {
            const auto left = registers[reg(tokens[2])];
            const auto right = registers[reg(tokens[3])];
            std::int64_t value = 0;
            if (op == "ADD") value = left + right;
            else if (op == "SUB") value = left - right;
            else if (op == "MUL") value = left * right;
            else if (op == "DIV") {
                if (right == 0) {
                    result.dynamic_error = true;
                    return result;
                }
                value = left / right;
            } else if (op == "CMP_EQ") value = left == right;
            else if (op == "CMP_NE") value = left != right;
            else if (op == "CMP_LT") value = left < right;
            else if (op == "CMP_LE") value = left <= right;
            else if (op == "CMP_GT") value = left > right;
            else if (op == "CMP_GE") value = left >= right;
            else require(false, "Operacion inesperada en el ejecutor acotado.");
            registers[reg(tokens[1])] = value;
        }
        ++pc;
    }
    return result;
}

void test_backends_are_observationally_equivalent_on_representative_programs() {
    const std::vector<std::string> programs = {
        "print(5);",
        "int x; x = (8 + 4) * 2 - 7 / 3; print(x);",
        "int x; x = -5; if (x < 0) { print(1); } else { print(0); }",
        "int x; x = 0; while (x < 5) { print(x); x = x + 1; }",
        "int x; x = 3; if (x != 4) { if (x <= 3) { print(x * x); } }"
    };
    for (const auto& source : programs) {
        lcero::common::DiagnosticBag reference_diagnostics;
        const auto reference = emit_lci(source, reference_diagnostics);
        lcero::common::DiagnosticBag optimized_diagnostics;
        const auto optimized = emit_optimized_lci(source, optimized_diagnostics);
        require(reference.succeeded && optimized.succeeded,
                "Ambos backends debian emitir el programa representativo.");
        const auto reference_execution = execute_lci_for_test(reference.text);
        const auto optimized_execution = execute_lci_for_test(optimized.text);
        require(reference_execution.halted && optimized_execution.halted &&
                    !reference_execution.step_limit_reached &&
                    !optimized_execution.step_limit_reached &&
                    reference_execution.output == optimized_execution.output,
                "Los backends no preservaron la misma salida y finalizacion.");
    }

    lcero::common::DiagnosticBag reference_diagnostics;
    const auto reference = emit_lci("print(10 / 0);", reference_diagnostics);
    lcero::common::DiagnosticBag optimized_diagnostics;
    const auto optimized = emit_optimized_lci("print(10 / 0);", optimized_diagnostics);
    require(execute_lci_for_test(reference.text).dynamic_error &&
                execute_lci_for_test(optimized.text).dynamic_error,
            "Ambos backends deben preservar el error dinamico de division por cero.");
}

void test_optimized_backend_preserves_large_decimal_literal() {
    lcero::common::DiagnosticBag diagnostics;
    const auto result = emit_optimized_lci(
        "print(0001234567890123456789012345678901234567890);",
        diagnostics);
    require(result.succeeded &&
                result.text.find("1234567890123456789012345678901234567890") !=
                    std::string::npos,
            "El backend optimizado limito o altero un entero grande.");
}

void test_backend_command_line_selection() {
    TemporaryDirectory temporary;
    const lcero::CommandLineParser parser;
    const auto reference = parser.parse(
        {"--backend", "reference"}, temporary.path(), temporary.path());
    const auto optimized = parser.parse(
        {"programa.lc"}, temporary.path(), temporary.path());
    require(reference.options.has_value() &&
                reference.options->backend == lcero::BackendKind::Reference,
            "--backend reference no selecciono el backend normal.");
    require(optimized.options.has_value() &&
                optimized.options->backend == lcero::BackendKind::Optimized,
            "Una invocacion con rutas debe usar optimized sin bloquear.");
    const auto invalid = parser.parse(
        {"--backend", "magico"}, temporary.path(), temporary.path());
    require(!invalid.options.has_value() && invalid.diagnostics.has_errors(),
            "Debia rechazarse un backend desconocido.");
}

void test_interactive_menu_handles_all_inputs() {
    const lcero::InteractiveMenu menu;
    for (const auto& item : std::vector<std::pair<std::string, lcero::BackendKind>>{
             {"1\n", lcero::BackendKind::Reference},
             {"2\n", lcero::BackendKind::Optimized},
             {"invalida\n2\n", lcero::BackendKind::Optimized}}) {
        std::istringstream input(item.first);
        std::ostringstream output;
        const auto result = menu.prompt(input, output, "Source/programa.lc");
        require(result.action == lcero::MenuAction::Compile &&
                    result.backend == item.second,
                "El menu no proceso correctamente una seleccion.");
        require(output.str().find("temporales") == std::string::npos &&
                    output.str().find("spilling") == std::string::npos,
                "El menu mostro metricas internas de optimizacion.");
    }
    std::istringstream exit_input("0\n");
    std::ostringstream exit_output;
    require(menu.prompt(exit_input, exit_output, "programa.lc").action ==
                lcero::MenuAction::Exit,
            "La opcion 0 no finalizo el menu.");
    std::istringstream eof_input;
    std::ostringstream eof_output;
    require(menu.prompt(eof_input, eof_output, "programa.lc").action ==
                lcero::MenuAction::EndOfInput,
            "El EOF interactivo no se manejo limpiamente.");
}

void test_compiler_pipeline_selects_optimized_backend() {
    TemporaryDirectory temporary;
    const auto source_path = temporary.path() / "programa.lc";
    const auto output_path = temporary.path() / "programa.lci";
    write_text(source_path, "int x; x = (1 + 2) * (3 + 4); print(x);\n");
    lcero::common::DiagnosticBag diagnostics;
    const lcero::Compiler compiler;
    const auto result = compiler.run(
        lcero::CompilerOptions{
            source_path, output_path, lcero::BackendKind::Optimized
        },
        diagnostics);
    require(result.status == lcero::CompilationStatus::Succeeded &&
                result.optimization_completed &&
                result.cfg_block_count > 0 &&
                result.interference_vertex_count == result.virtual_temporary_count &&
                result.lci_emission_completed,
            "El pipeline no ejecuto realmente el backend optimizado.");
    const lcero::ir::LciValidator validator;
    require(validator.validate(read_text(output_path), output_path, diagnostics),
            "El pipeline optimizado publico un archivo invalido.");
}

std::string token_set_text(const std::set<lcero::lexer::TokenType>& tokens) {
    std::string result;
    for (const auto token : tokens) {
        if (!result.empty()) {
            result += '|';
        }
        result += lcero::lexer::token_type_name(token);
    }
    return result.empty() ? "-" : result;
}

void test_documented_grammar_tables_match_analysis() {
    const lcero::parser::GrammarAnalysis analysis;
    std::ostringstream first_document;
    std::ostringstream follow_document;
    std::ostringstream select_document;
    std::ostringstream table_document;
    first_document << "nonterminal,nullable,FIRST\n";
    follow_document << "nonterminal,FOLLOW\n";
    select_document << "production,SELECT\n";
    table_document << "nonterminal,terminal,production\n";

    for (const auto nonterminal : analysis.grammar().nonterminals()) {
        const auto name = lcero::parser::nonterminal_name(nonterminal);
        first_document << name << ','
                       << (analysis.nullable(nonterminal) ? "yes" : "no") << ','
                       << token_set_text(analysis.first(nonterminal)) << '\n';
        follow_document << name << ','
                        << token_set_text(analysis.follow(nonterminal)) << '\n';
        for (const auto terminal : analysis.grammar().terminals()) {
            const auto production = analysis.production_for(nonterminal, terminal);
            if (production.has_value()) {
                table_document << name << ','
                               << lcero::lexer::token_type_name(terminal) << ','
                               << production.value() << '\n';
            }
        }
    }
    for (const auto& production : analysis.grammar().productions()) {
        select_document << production.number << ','
                        << token_set_text(analysis.select(production.number)) << '\n';
    }

    const auto project_root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto grammar_docs = project_root / "docs/gramatica";
    require(read_text(grammar_docs / "first.csv") == first_document.str(),
            "first.csv no coincide con el calculo ejecutable.");
    require(read_text(grammar_docs / "follow.csv") == follow_document.str(),
            "follow.csv no coincide con el calculo ejecutable.");
    require(read_text(grammar_docs / "select.csv") == select_document.str(),
            "select.csv no coincide con el calculo ejecutable.");
    require(read_text(grammar_docs / "tabla_ll1.csv") == table_document.str(),
            "tabla_ll1.csv no coincide con la tabla ejecutable.");
}

void print_optimizer_report() {
    const std::vector<std::pair<std::string, std::string>> programs = {
        {"constante", "print(5);"},
        {"aritmetica", "int x; x = (1 + 2) * (3 + 4); print(x);"},
        {"condicional", "int x; x = -5; if (x < 0) { print(1); } else { print(0); }"},
        {"bucle", "int x; x = 0; while (x < 5) { print(x); x = x + 1; }"},
        {"anidado", "int x; x = 3; if (x != 4) { if (x <= 3) { print(x * x); } }"}
    };
    std::cout << "programa,ri,temp,bloques,cfg_aristas,ig_vertices,ig_aristas,"
                 "spills,ref_mem,ref_inst,ref_ls,opt_mem,opt_inst,opt_ls\n";
    for (const auto& item : programs) {
        lcero::common::DiagnosticBag generation_diagnostics;
        const auto generation = generate_ir(item.second, generation_diagnostics);
        lcero::common::DiagnosticBag reference_diagnostics;
        const lcero::ir::ReferenceBackend reference_backend;
        const auto reference = reference_backend.emit(
            generation.program, "referencia.lci", reference_diagnostics);
        lcero::common::DiagnosticBag optimized_diagnostics;
        const lcero::ir::OptimizedBackend optimized_backend;
        const auto optimized = optimized_backend.emit(
            generation.program, "optimizado.lci", optimized_diagnostics);
        require(generation.succeeded && reference.succeeded && optimized.succeeded,
                "No se pudieron medir los backends.");
        std::cout << item.first << ','
                  << generation.program.instructions.size() << ','
                  << generation.program.temporary_count << ','
                  << optimized.cfg_block_count << ','
                  << optimized.cfg_edge_count << ','
                  << optimized.interference_vertex_count << ','
                  << optimized.interference_edge_count << ','
                  << optimized.spilled_temporary_count << ','
                  << reference.memory_slot_count << ','
                  << reference.physical_instruction_count << ','
                  << reference.load_count + reference.store_count << ','
                  << optimized.memory_slot_count << ','
                  << optimized.physical_instruction_count << ','
                  << optimized.load_count + optimized.store_count << '\n';
    }
}

void print_grammar_report() {
    const lcero::parser::GrammarAnalysis analysis;
    std::cout << "NONTERMINALS=" << analysis.grammar().nonterminals().size() << '\n'
              << "TERMINALS=" << analysis.grammar().terminals().size() << '\n'
              << "PRODUCTIONS=" << analysis.grammar().productions().size() << '\n'
              << "NULLABLE_PASSES=" << analysis.nullable_pass_count() << '\n'
              << "FIRST_PASSES=" << analysis.first_pass_count() << '\n'
              << "FOLLOW_PASSES=" << analysis.follow_pass_count() << '\n'
              << "CONFLICTS=" << analysis.conflicts().size() << "\n\n";

    std::cout << "PRODUCTIONS\n";
    for (const auto& production : analysis.grammar().productions()) {
        std::cout << lcero::parser::production_text(production) << '\n';
    }
    std::cout << "\nSETS\nnonterminal,nullable,FIRST,FOLLOW\n";
    for (const auto nonterminal : analysis.grammar().nonterminals()) {
        std::cout << lcero::parser::nonterminal_name(nonterminal) << ','
                  << (analysis.nullable(nonterminal) ? "yes" : "no") << ','
                  << token_set_text(analysis.first(nonterminal)) << ','
                  << token_set_text(analysis.follow(nonterminal)) << '\n';
    }
    std::cout << "\nSELECT\nproduction,terminals\n";
    for (const auto& production : analysis.grammar().productions()) {
        std::cout << production.number << ','
                  << token_set_text(analysis.select(production.number)) << '\n';
    }
    std::cout << "\nTABLE\nnonterminal,terminal,production\n";
    for (const auto nonterminal : analysis.grammar().nonterminals()) {
        for (const auto terminal : analysis.grammar().terminals()) {
            const auto production = analysis.production_for(nonterminal, terminal);
            if (production.has_value()) {
                std::cout << lcero::parser::nonterminal_name(nonterminal) << ','
                          << lcero::lexer::token_type_name(terminal) << ','
                          << production.value() << '\n';
            }
        }
    }
}

void print_automaton_report() {
    const lcero::lexer::LexerAutomata automata;
    const auto& dfa = automata.determinized_dfa();
    const auto& minimized = automata.minimized_dfa();
    const auto classes = minimized.symbol_classes();

    std::cout << "NFA_STATES=" << automata.nfa().state_count() << '\n'
              << "DFA_STATES=" << dfa.state_count() << '\n'
              << "MIN_DFA_STATES=" << minimized.state_count() << '\n'
              << "MIN_SINK=M" << minimized.sink_state() << '\n'
              << "SYMBOL_CLASSES=" << classes.size() << '\n';

    for (std::size_t class_id = 0; class_id < classes.size(); ++class_id) {
        std::cout << "C" << class_id << '=';
        for (std::size_t index = 0; index < classes[class_id].size(); ++index) {
            if (index != 0) {
                std::cout << '|';
            }
            std::cout << lcero::lexer::display_symbol(classes[class_id][index]);
        }
        std::cout << '\n';
    }

    std::cout << "STATE,ACCEPT,ORIGINS";
    for (std::size_t class_id = 0; class_id < classes.size(); ++class_id) {
        std::cout << ",C" << class_id;
    }
    std::cout << '\n';

    for (lcero::lexer::StateId state = 0; state < minimized.state_count(); ++state) {
        const auto& item = minimized.states()[state];
        std::cout << 'M' << state << ',';
        if (item.acceptance.has_value()) {
            std::cout << lcero::lexer::token_type_name(item.acceptance->token_type);
        } else {
            std::cout << '-';
        }
        std::cout << ',';
        for (std::size_t index = 0; index < item.origins.size(); ++index) {
            if (index != 0) {
                std::cout << '|';
            }
            std::cout << 'D' << item.origins[index];
        }
        for (const auto& symbol_class : classes) {
            std::cout << ",M"
                      << item.transitions[symbol_class.front()];
        }
        std::cout << '\n';
    }

    std::cout << "\nDFA_SUBSETS\n";
    for (lcero::lexer::StateId state = 0; state < dfa.state_count(); ++state) {
        const auto& item = dfa.states()[state];
        std::cout << 'D' << state << ',';
        if (item.acceptance.has_value()) {
            std::cout << lcero::lexer::token_type_name(item.acceptance->token_type);
        } else {
            std::cout << '-';
        }
        std::cout << ',';
        for (std::size_t index = 0; index < item.origins.size(); ++index) {
            if (index != 0) {
                std::cout << '|';
            }
            std::cout << 'N' << item.origins[index];
        }
        std::cout << '\n';
    }

    std::cout << "\nMINIMIZATION_TRACE\n";
    const auto trace = dfa.minimization_partition_trace();
    for (std::size_t round = 0; round < trace.size(); ++round) {
        std::cout << 'P' << round << '=';
        for (std::size_t block = 0; block < trace[round].size(); ++block) {
            if (block != 0) {
                std::cout << ';';
            }
            std::cout << '{';
            for (std::size_t index = 0; index < trace[round][block].size(); ++index) {
                if (index != 0) {
                    std::cout << '|';
                }
                std::cout << 'D' << trace[round][block][index];
            }
            std::cout << '}';
        }
        std::cout << '\n';
    }
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--automaton-report") {
        print_automaton_report();
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--grammar-report") {
        print_grammar_report();
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--optimizer-report") {
        print_optimizer_report();
        return 0;
    }

    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"lectura de fuente existente", test_reads_existing_source},
        {"archivo fuente inexistente", test_reports_missing_source},
        {"error de lectura reproducible", test_reports_non_regular_source},
        {"rutas predeterminadas", test_uses_default_paths},
        {"rutas por argumentos", test_uses_argument_paths},
        {"salida derivada", test_derives_output_for_input_only},
        {"argumentos invalidos", test_rejects_invalid_arguments},
        {"raiz desde directorio de trabajo", test_finds_root_from_working_directory},
        {"raiz desde ejecutable", test_finds_root_from_executable},
        {"generacion integrada de LCI", test_compiler_generates_valid_lci},
        {"error lexico integrado", test_compiler_stops_on_lexical_error},
        {"error sintactico integrado", test_compiler_stops_on_syntactic_error},
        {"publicacion transaccional", test_publishes_output_transactionally},
        {"error de escritura sin salida parcial", test_reports_output_write_error_without_partial_file},
        {"reservadas e identificadores", test_lexes_keywords_and_identifiers},
        {"numeros grandes y menos separado", test_lexes_numbers_and_unary_minus_separately},
        {"operadores y delimitadores", test_lexes_all_operators_and_delimiters},
        {"posiciones LF y CRLF", test_tracks_lf_and_crlf_positions},
        {"instrucciones y bloques anidados", test_lexes_multiple_and_nested_statements},
        {"fuente vacio", test_empty_source_produces_only_eof},
        {"ausencia de comentarios", test_does_not_treat_slashes_as_comments},
        {"caracter invalido y posicion", test_reports_invalid_character_position},
        {"admiracion aislada", test_reports_isolated_bang},
        {"operadores y decimal no admitidos", test_reports_disallowed_operators_and_decimal_point},
        {"caracteres no ASCII", test_reports_non_ascii_characters},
        {"coincidencia mas larga", test_maximal_munch},
        {"AFD determinista, total y alcanzable", test_dfa_is_deterministic_complete_and_reachable},
        {"clasificacion de estados finales", test_accepting_state_classification},
        {"consistencia de matriz", test_transition_matrix_is_consistent},
        {"equivalencia antes y despues de minimizar", test_minimization_preserves_observable_language},
        {"ausencia de estados fusionables", test_minimized_dfa_has_no_mergeable_states},
        {"comparacion AFN y AFD sobre cadenas", test_nfa_and_dfas_agree_on_generated_strings},
        {"programas sintacticamente validos", test_parser_accepts_representative_programs},
        {"seis comparadores sintacticos", test_parser_accepts_every_comparator},
        {"programas sintacticamente invalidos", test_parser_rejects_invalid_programs},
        {"diagnostico sintactico y posicion", test_parser_reports_expected_token_and_position},
        {"precedencia y asociatividad del AST", test_ast_preserves_precedence_and_associativity},
        {"unario, parentesis y literales del AST", test_ast_represents_unary_parentheses_and_large_literals},
        {"control y orden del AST", test_ast_represents_control_flow_and_order},
        {"inventario formal y anulables", test_grammar_inventory_and_nullable_set},
        {"conjuntos FIRST y FOLLOW", test_first_and_follow_sets},
        {"SELECT y tabla LL(1)", test_select_sets_and_parse_table_are_ll1},
        {"documentacion formal sincronizada", test_documented_grammar_tables_match_analysis},
        {"tabla global de simbolos", test_symbol_table_tracks_global_declarations},
        {"programas semanticamente validos", test_semantic_accepts_representative_programs},
        {"declaracion duplicada y ubicacion previa", test_semantic_reports_duplicate_with_previous_location},
        {"variables no declaradas", test_semantic_rejects_undeclared_variables},
        {"lecturas no inicializadas", test_semantic_rejects_uninitialized_reads},
        {"transferencia conservadora de if", test_semantic_if_is_conservative},
        {"transferencia conservadora de while", test_semantic_while_does_not_propagate_initialization},
        {"errores semanticos anidados", test_semantic_finds_errors_inside_nested_structures},
        {"orden y visibilidad global", test_semantic_respects_order_and_global_visibility},
        {"conjuntos de transferencia e invariante", test_semantic_transfer_sets_and_invariant},
        {"error semantico integrado", test_compiler_stops_on_semantic_error},
        {"RI de expresiones y direcciones", test_ir_preserves_expression_structure_and_variable_order},
        {"literales grandes canonicos en RI", test_ir_canonicalizes_large_literals_without_numeric_conversion},
        {"comparadores y control en RI", test_ir_maps_every_comparison_and_control_flow},
        {"recursos del backend de referencia", test_reference_backend_uses_only_contract_resources},
        {"validacion estricta de LCERO_IR 1", test_lci_validator_accepts_contract_and_rejects_malformed_text},
        {"rechazo de RI interna inconsistente", test_reference_backend_rejects_inconsistent_internal_ir},
        {"CFG con ramas y ciclo", test_cfg_builds_basic_blocks_edges_and_cycle},
        {"punto fijo de vivacidad", test_liveness_reaches_consistent_fixed_point},
        {"grafo de interferencia", test_interference_is_symmetric_precise_and_loop_aware},
        {"coloreo y spilling deterministas", test_chaitin_colors_and_spills_deterministically},
        {"backend optimizado y spilling real", test_optimized_backend_emits_coloring_and_real_spills},
        {"equivalencia observable de backends", test_backends_are_observationally_equivalent_on_representative_programs},
        {"literal grande en backend optimizado", test_optimized_backend_preserves_large_decimal_literal},
        {"seleccion de backend por argumentos", test_backend_command_line_selection},
        {"menu interactivo simulado", test_interactive_menu_handles_all_inputs},
        {"pipeline optimizado integrado", test_compiler_pipeline_selects_optimized_backend}
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[OK] " << test.first << '\n';
        } catch (const std::exception& exception) {
            ++failures;
            std::cerr << "[FALLO] " << test.first << ": "
                      << exception.what() << '\n';
        }
    }

    std::cout << "\n" << (tests.size() - static_cast<std::size_t>(failures))
              << '/' << tests.size() << " pruebas aprobadas.\n";
    return failures == 0 ? 0 : 1;
}
