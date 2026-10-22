#include "lcero/compiler_options.hpp"

#include "lcero/common/path_resolver.hpp"

#include <utility>

namespace lcero {
namespace {

void report_argument_error(
    common::DiagnosticBag& diagnostics,
    std::string message) {
    diagnostics.report(
        common::DiagnosticCategory::CommandLine,
        common::DiagnosticSeverity::Error,
        std::move(message));
}

bool has_expected_extension(
    const std::filesystem::path& path,
    const std::filesystem::path& expected_extension) {
    return path.extension() == expected_extension;
}

} // namespace

CommandLineParseResult CommandLineParser::parse(
    const std::vector<std::string>& arguments,
    const std::filesystem::path& project_root,
    const std::filesystem::path& current_directory) const {
    CommandLineParseResult result;
    std::optional<std::filesystem::path> input_argument;
    std::optional<std::filesystem::path> output_argument;
    std::optional<BackendKind> backend_argument;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];

        if (argument == "--help" || argument == "-h") {
            if (arguments.size() != 1) {
                report_argument_error(
                    result.diagnostics,
                    "--help no puede combinarse con otros argumentos.");
            } else {
                result.show_help = true;
            }
            continue;
        }

        if (argument == "-o") {
            if (output_argument.has_value()) {
                report_argument_error(
                    result.diagnostics,
                    "La opcion -o solo puede indicarse una vez.");
                continue;
            }
            if (index + 1 >= arguments.size()) {
                report_argument_error(
                    result.diagnostics,
                    "Falta la ruta de salida despues de -o.");
                continue;
            }

            output_argument = std::filesystem::path(arguments[++index]);
            continue;
        }

        if (argument == "--backend") {
            if (backend_argument.has_value()) {
                report_argument_error(
                    result.diagnostics,
                    "La opcion --backend solo puede indicarse una vez.");
                continue;
            }
            if (index + 1 >= arguments.size()) {
                report_argument_error(
                    result.diagnostics,
                    "Falta reference u optimized despues de --backend.");
                continue;
            }
            const auto& value = arguments[++index];
            if (value == "reference") {
                backend_argument = BackendKind::Reference;
            } else if (value == "optimized") {
                backend_argument = BackendKind::Optimized;
            } else {
                report_argument_error(
                    result.diagnostics,
                    "Backend desconocido: " + value +
                        ". Use reference u optimized.");
            }
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            report_argument_error(
                result.diagnostics,
                "Opcion desconocida: " + argument);
            continue;
        }

        if (input_argument.has_value()) {
            report_argument_error(
                result.diagnostics,
                "Solo se admite un archivo de entrada.");
            continue;
        }

        input_argument = std::filesystem::path(argument);
    }

    if (result.show_help || result.diagnostics.has_errors()) {
        return result;
    }

    const auto input = input_argument.has_value()
        ? common::PathResolver::resolve_from(current_directory, input_argument.value())
        : common::PathResolver::resolve_from(project_root, "Source/programa.lc");

    std::filesystem::path output;
    if (output_argument.has_value()) {
        output = common::PathResolver::resolve_from(
            current_directory,
            output_argument.value());
    } else if (input_argument.has_value()) {
        output = project_root / "LCIs" / input.stem();
        output.replace_extension(".lci");
        output = output.lexically_normal();
    } else {
        output = common::PathResolver::resolve_from(
            project_root,
            "LCIs/programa.lci");
    }

    if (!has_expected_extension(input, ".lc")) {
        report_argument_error(
            result.diagnostics,
            "El archivo de entrada debe tener extension .lc.");
    }
    if (!has_expected_extension(output, ".lci")) {
        report_argument_error(
            result.diagnostics,
            "El archivo de salida debe tener extension .lci.");
    }

    if (!result.diagnostics.has_errors()) {
        // Toda invocacion con argumentos y sin seleccion explicita usa el
        // backend optimizado; asi ningun script queda esperando el menu.
        result.options = CompilerOptions{
            input,
            output,
            backend_argument.value_or(BackendKind::Optimized)
        };
    }

    return result;
}

} // namespace lcero
