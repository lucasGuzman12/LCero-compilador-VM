#include "lcero/common/source_reader.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace lcero::common {

std::optional<SourceFile> SourceReader::read(
    const std::filesystem::path& path,
    DiagnosticBag& diagnostics) const {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        diagnostics.report(
            DiagnosticCategory::InputOutput,
            DiagnosticSeverity::Error,
            "No se pudo consultar el archivo de entrada: " + error.message(),
            SourceLocation{path});
        return std::nullopt;
    }

    if (!exists) {
        diagnostics.report(
            DiagnosticCategory::InputOutput,
            DiagnosticSeverity::Error,
            "El archivo de entrada no existe.",
            SourceLocation{path});
        return std::nullopt;
    }

    if (!std::filesystem::is_regular_file(path, error) || error) {
        diagnostics.report(
            DiagnosticCategory::InputOutput,
            DiagnosticSeverity::Error,
            "La ruta de entrada no corresponde a un archivo regular legible.",
            SourceLocation{path});
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        diagnostics.report(
            DiagnosticCategory::InputOutput,
            DiagnosticSeverity::Error,
            "No se pudo abrir el archivo de entrada para lectura.",
            SourceLocation{path});
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        diagnostics.report(
            DiagnosticCategory::InputOutput,
            DiagnosticSeverity::Error,
            "Ocurrio un error mientras se leia el archivo de entrada.",
            SourceLocation{path});
        return std::nullopt;
    }

    return SourceFile{path, buffer.str()};
}

} // namespace lcero::common
