#include "lcero/common/diagnostic.hpp"
#include "lcero/common/diagnostic_renderer.hpp"
#include "lcero/common/path_resolver.hpp"
#include "lcero/compiler.hpp"
#include "lcero/compiler_options.hpp"
#include "lcero/console_menu.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

void print_usage(std::ostream& output) {
    output
        << "Uso:\n"
        << "  LCero.Compiler.exe\n"
        << "  LCero.Compiler.exe --backend reference|optimized\n"
        << "  LCero.Compiler.exe archivo.lc [-o archivo.lci] "
           "[--backend reference|optimized]\n"
        << "  LCero.Compiler.exe -h | --help\n\n"
        << "Sin argumentos se muestra el menu interactivo. Con rutas y sin "
           "--backend se usa optimized.\n";
}

void print_successful_stages(
    const lcero::CompilationResult& result,
    const std::filesystem::path& output_path) {
    std::cout << "[OK] Lectura del archivo\n"
              << "[OK] Analisis lexico\n"
              << "[OK] Analisis sintactico\n"
              << "[OK] Analisis semantico\n"
              << "[OK] Generacion de representacion intermedia\n";
    if (result.backend == lcero::BackendKind::Optimized) {
        std::cout << "[OK] Optimizacion con Chaitin\n";
    } else {
        std::cout << "[OK] Generacion sin optimizacion\n";
    }
    std::cout << "[OK] Validacion LCERO_IR 1\n"
              << "[OK] Archivo generado correctamente\n\n"
              << "----------------------------------------\n"
              << "COMPILACION EXITOSA\n"
              << "Salida: " << output_path.string() << '\n'
              << "----------------------------------------\n";
}

std::filesystem::path determine_executable_path(const char* argument_zero) {
    if (argument_zero == nullptr || *argument_zero == '\0') {
        return {};
    }

    std::error_code error;
    const auto path = std::filesystem::absolute(argument_zero, error);
    return error ? std::filesystem::path(argument_zero) : path;
}

} // namespace

int main(int argc, char* argv[]) {
    using lcero::common::DiagnosticCategory;
    using lcero::common::DiagnosticSeverity;

    lcero::common::DiagnosticBag startup_diagnostics;
    lcero::common::DiagnosticRenderer renderer;

    std::error_code error;
    const auto current_directory = std::filesystem::current_path(error);
    if (error) {
        startup_diagnostics.report(
            DiagnosticCategory::InputOutput,
            DiagnosticSeverity::Error,
            "No se pudo determinar el directorio de trabajo: " + error.message());
        renderer.render(startup_diagnostics, std::cerr);
        return 1;
    }

    const auto executable_path = determine_executable_path(argc > 0 ? argv[0] : nullptr);
    const auto project_root = lcero::common::PathResolver::find_project_root(
        current_directory,
        executable_path);
    if (!project_root.has_value()) {
        startup_diagnostics.report(
            DiagnosticCategory::InputOutput,
            DiagnosticSeverity::Error,
            "No se encontro la raiz del proyecto (falta LCero-Compilador.vcxproj "
            "en los directorios ascendentes). Ejecute desde el proyecto o conserve "
            "la estructura de salida configurada.");
        renderer.render(startup_diagnostics, std::cerr);
        return 1;
    }

    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const lcero::CommandLineParser command_line_parser;
    auto parse_result = command_line_parser.parse(
        arguments,
        project_root.value(),
        current_directory);

    if (parse_result.show_help) {
        print_usage(std::cout);
        return 0;
    }

    if (parse_result.diagnostics.has_errors() || !parse_result.options.has_value()) {
        renderer.render(parse_result.diagnostics, std::cerr);
        print_usage(std::cerr);
        return 1;
    }

    auto options = parse_result.options.value();
    if (arguments.empty()) {
        const lcero::InteractiveMenu menu;
        const auto selection = menu.prompt(std::cin, std::cout, options.input_path);
        if (selection.action != lcero::MenuAction::Compile) {
            return 0;
        }
        options.backend = selection.backend.value();
    } else {
        std::cout << "========================================\n"
                  << "             L-CERO COMPILER\n"
                  << "========================================\n\n";
    }

    std::cout << "Modo seleccionado: "
              << (options.backend == lcero::BackendKind::Optimized
                      ? "OPTIMIZADO" : "NORMAL")
              << "\nArchivo: " << options.input_path.string() << "\n\n";

    lcero::common::DiagnosticBag compilation_diagnostics;
    const lcero::Compiler compiler;
    const auto compilation_result = compiler.run(options, compilation_diagnostics);
    switch (compilation_result.status) {
    case lcero::CompilationStatus::Succeeded:
        print_successful_stages(compilation_result, options.output_path);
        return 0;
    case lcero::CompilationStatus::Failed:
        renderer.render(compilation_diagnostics, std::cerr);
        std::cerr << "[ERROR] Compilacion detenida.\n";
        return 1;
    }

    return 1;
}
