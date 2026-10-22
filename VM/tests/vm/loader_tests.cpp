#include "lcero/vm/diagnostics.hpp"
#include "lcero/vm/instruction.hpp"
#include "lcero/vm/ir_loader.hpp"
#include "lcero/vm/ir_validator.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using lcero::vm::Diagnostic;
using lcero::vm::DiagnosticCategory;
using lcero::vm::DiagnosticCode;
using lcero::vm::JumpTarget;
using lcero::vm::ValidatedIr;

int failures = 0;

void expect(const bool condition, const std::string& description) {
    if (condition) {
        std::cout << "[OK] " << description << '\n';
        return;
    }
    ++failures;
    std::cerr << "[FALLO] " << description << '\n';
}

struct CheckOutcome {
    std::optional<ValidatedIr> validated;
    std::optional<Diagnostic> diagnostic;

    [[nodiscard]] bool valid() const noexcept {
        return validated.has_value() && !diagnostic.has_value();
    }
};

CheckOutcome check_file(const std::filesystem::path& path) {
    auto loaded = lcero::vm::load_ir_source(path);
    if (!loaded.ok()) {
        return {std::nullopt, std::move(loaded.diagnostic)};
    }

    auto validated = lcero::vm::validate_ir(*loaded.value);
    if (!validated.ok()) {
        return {std::nullopt, std::move(validated.diagnostic)};
    }
    return {std::move(validated.value), std::nullopt};
}

std::vector<std::filesystem::path> lci_files(
    const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".lci") {
            files.push_back(entry.path());
        }
    }
    return files;
}

void test_contract_suite(const std::filesystem::path& root) {
    std::size_t accepted = 0U;
    for (const std::string category : {"valid", "limit", "runtime_error"}) {
        for (const auto& path : lci_files(root / category)) {
            const CheckOutcome result = check_file(path);
            expect(result.valid(),
                   category + "/" + path.filename().string() +
                       " se acepta en carga");
            if (result.valid()) {
                ++accepted;
            }
        }
    }
    expect(accepted == 24U,
           "se aceptan los 24 fixtures estaticamente validos");

    std::size_t rejected = 0U;
    for (const auto& path : lci_files(root / "load_error")) {
        const CheckOutcome result = check_file(path);
        const bool is_load_error =
            !result.valid() && result.diagnostic.has_value() &&
            result.diagnostic->category == DiagnosticCategory::load;
        expect(is_load_error,
               "load_error/" + path.filename().string() +
                   " se rechaza como ERROR_DE_CARGA");
        if (is_load_error) {
            ++rejected;
        }
    }
    expect(rejected == 16U, "se rechazan los 16 fixtures load_error");
}

void expect_error(const std::filesystem::path& path,
                  const DiagnosticCode code,
                  const std::string& description) {
    const CheckOutcome result = check_file(path);
    expect(!result.valid() && result.diagnostic.has_value() &&
               result.diagnostic->code == code,
           description);
}

void test_essential_diagnostics(const std::filesystem::path& root) {
    expect_error(root / "load_error/25_version_invalida.lci",
                 DiagnosticCode::invalid_header,
                 "LCERO_IR 2 se diagnostica como version invalida");
    expect_error(root / "load_error/30_etiqueta_inexistente.lci",
                 DiagnosticCode::unknown_label,
                 "un salto a etiqueta inexistente falla en segunda pasada");
    expect_error(root / "load_error/31_etiqueta_duplicada.lci",
                 DiagnosticCode::duplicate_label,
                 "una segunda definicion de etiqueta se rechaza");
    expect_error(root / "load_error/32_inmediato_menos_cero.lci",
                 DiagnosticCode::invalid_immediate_syntax,
                 "el inmediato -0 conserva su diagnostico especifico");
    expect_error(root / "load_error/35_halt_ausente.lci",
                 DiagnosticCode::missing_halt,
                 "un archivo sin HALT falla aunque su flujo pudiera continuar");
    expect_error(root / "load_error/36_comentario_no_admitido.lci",
                 DiagnosticCode::invalid_operand,
                 "un comentario se rechaza como texto ajeno a la gramatica");
}

void test_resolved_labels(const std::filesystem::path& root) {
    const CheckOutcome loop =
        check_file(root / "valid/08_bucle_descendente.lci");
    expect(loop.valid(), "el bucle descendente pasa ambas pasadas");
    if (loop.valid()) {
        const auto* forward = std::get_if<JumpTarget>(
            &loop.validated->program.at(5U).operands.at(1U));
        const auto* backward = std::get_if<JumpTarget>(
            &loop.validated->program.at(12U).operands.at(0U));
        expect(forward != nullptr && forward->instruction_index == 13U,
               "JZ resuelve la etiqueta fin al indice 13 de P");
        expect(backward != nullptr && backward->instruction_index == 2U,
               "JMP resuelve la etiqueta ciclo al indice 2 de P");
        expect(loop.validated->labels.at("ciclo") == 2U &&
                   loop.validated->labels.at("fin") == 13U,
               "ValidatedIr conserva LABEL a pc para inspeccion y trazas");
    }

    const CheckOutcome terminal =
        check_file(root / "runtime_error/23_caida_sin_halt.lci");
    expect(terminal.valid(), "una etiqueta al EOF es valida en carga");
    if (terminal.valid()) {
        const auto* target = std::get_if<JumpTarget>(
            &terminal.validated->program.at(1U).operands.at(0U));
        expect(target != nullptr &&
                   target->instruction_index == terminal.validated->program.size(),
               "la etiqueta terminal resuelve exactamente a |P|");
        expect(terminal.validated->labels.at("fin") ==
                   terminal.validated->program.size(),
               "la tabla observable conserva una etiqueta ubicada en EOF");
    }

    expect(check_file(root / "runtime_error/19_division_por_cero.lci").valid(),
           "DIV por cero se acepta porque es un error dinamico");
}

std::filesystem::path make_temporary_directory() {
    const auto suffix = std::chrono::high_resolution_clock::now()
                            .time_since_epoch()
                            .count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("lcero_loader_tests_" + std::to_string(suffix));
    std::filesystem::create_directory(directory);
    return directory;
}

void write_binary(const std::filesystem::path& path,
                  const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void test_line_endings_and_resources() {
    const std::filesystem::path temporary = make_temporary_directory();
    const std::filesystem::path lf = temporary / "solo_lf.lci";
    const std::filesystem::path crlf = temporary / "solo_crlf.lci";
    const std::filesystem::path version2 = temporary / "version2.lci";
    const std::filesystem::path bare_cr = temporary / "cr_solo.lci";
    const std::filesystem::path huge = temporary / "enorme.lci";
    const std::filesystem::path absent = temporary / "ausente.lci";

    write_binary(lf, "LCERO_IR 1\nMEM 0\nHALT\n");
    write_binary(crlf, "LCERO_IR 1\r\nMEM 0\r\nHALT\r\n");
    write_binary(version2, "LCERO_IR 2\nMEM 0\nHALT\n");
    write_binary(bare_cr, "LCERO_IR 1\rMEM 0\rHALT\r");

    expect(check_file(lf).valid(), "un archivo propio con LF es valido");
    expect(check_file(crlf).valid(), "un archivo propio con CRLF es valido");
    expect_error(version2, DiagnosticCode::invalid_header,
                 "LCERO_IR 2 propio siempre se rechaza");
    expect_error(bare_cr, DiagnosticCode::invalid_line_ending,
                 "CR aislado no se confunde con CRLF");

    const auto inaccessible = lcero::vm::load_ir_source(absent);
    expect(!inaccessible.ok() && inaccessible.diagnostic->category ==
                                      DiagnosticCategory::resource &&
               inaccessible.diagnostic->code ==
                   DiagnosticCode::file_open_failed,
           "un archivo inexistente es fallo de recurso, no de gramatica");

    {
        std::ofstream output(huge, std::ios::binary | std::ios::trunc);
        output.seekp(static_cast<std::streamoff>(
            lcero::vm::kMaximumIrFileSizeBytes));
        output.put('x');
    }
    const auto oversized = lcero::vm::load_ir_source(huge);
    expect(!oversized.ok() && oversized.diagnostic->category ==
                                  DiagnosticCategory::resource &&
               oversized.diagnostic->code == DiagnosticCode::file_too_large,
           "un archivo mayor de 16 MiB se diagnostica como recurso");

    // Se eliminan solo los archivos temporales creados por esta prueba.
    std::error_code cleanup_error;
    for (const auto& path : {lf, crlf, version2, bare_cr, huge}) {
        std::filesystem::remove(path, cleanup_error);
        expect(!cleanup_error, "se elimina el fixture temporal " +
                                   path.filename().string());
        cleanup_error.clear();
    }
    std::filesystem::remove(temporary, cleanup_error);
    expect(!cleanup_error, "se elimina el directorio temporal propio");
}

}  // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Uso de prueba: lcero_vm_loader_tests <contracts-dir>\n";
        return 2;
    }

    const std::filesystem::path contracts(argv[1]);
    test_contract_suite(contracts);
    test_essential_diagnostics(contracts);
    test_resolved_labels(contracts);
    test_line_endings_and_resources();

    if (failures == 0) {
        std::cout << "Todas las pruebas del loader y validador pasaron.\n";
        return 0;
    }
    std::cerr << failures << " prueba(s) del loader fallaron.\n";
    return 1;
}
