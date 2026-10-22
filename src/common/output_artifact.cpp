#include "lcero/common/output_artifact.hpp"

#include <fstream>
#include <string>
#include <system_error>

namespace lcero::common {
namespace {

void report_output_error(
    DiagnosticBag& diagnostics,
    const std::filesystem::path& path,
    const std::string& message) {
    diagnostics.report(
        DiagnosticCategory::CodeGeneration,
        DiagnosticSeverity::Error,
        message,
        SourceLocation{path});
}

} // namespace

bool OutputArtifact::invalidate_previous(
    const std::filesystem::path& output_path,
    DiagnosticBag& diagnostics) const {
    std::error_code error;
    const auto temporary_path = temporary_path_for(output_path);

    std::filesystem::remove(temporary_path, error);
    if (error) {
        report_output_error(
            diagnostics,
            temporary_path,
            "No se pudo eliminar un archivo temporal anterior: " + error.message());
        return false;
    }

    error.clear();
    const bool removed = std::filesystem::remove(output_path, error);
    if (error) {
        report_output_error(
            diagnostics,
            output_path,
            "No se pudo invalidar la salida de una compilacion anterior: " +
                error.message());
        return false;
    }

    if (removed) {
        diagnostics.report(
            DiagnosticCategory::CodeGeneration,
            DiagnosticSeverity::Information,
            "Se invalido el archivo de salida anterior para evitar confundirlo "
            "con el resultado de esta ejecucion.",
            SourceLocation{output_path});
    }

    return true;
}

bool OutputArtifact::publish(
    const std::filesystem::path& output_path,
    const std::string_view content,
    DiagnosticBag& diagnostics) const {
    const auto temporary_path = temporary_path_for(output_path);
    std::error_code error;

    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            report_output_error(
                diagnostics,
                output_path,
                "No se pudo crear el directorio de salida: " + error.message());
            return false;
        }
    }

    error.clear();
    std::filesystem::remove(temporary_path, error);
    if (error) {
        report_output_error(
            diagnostics,
            temporary_path,
            "No se pudo preparar el archivo temporal: " + error.message());
        return false;
    }

    {
        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            report_output_error(
                diagnostics,
                temporary_path,
                "No se pudo abrir el archivo temporal para escritura.");
            return false;
        }

        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary_path, error);
            report_output_error(
                diagnostics,
                temporary_path,
                "No se pudo escribir por completo el archivo temporal.");
            return false;
        }
    }

    error.clear();
    if (std::filesystem::exists(output_path, error) || error) {
        std::filesystem::remove(temporary_path, error);
        report_output_error(
            diagnostics,
            output_path,
            "La salida final reaparecio antes de la publicacion; no se la sobrescribio.");
        return false;
    }

    std::filesystem::rename(temporary_path, output_path, error);
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_path, cleanup_error);
        report_output_error(
            diagnostics,
            output_path,
            "No se pudo publicar el archivo de salida: " + error.message());
        return false;
    }

    return true;
}

std::filesystem::path OutputArtifact::temporary_path_for(
    const std::filesystem::path& output_path) {
    auto temporary_path = output_path;
    temporary_path += ".tmp";
    return temporary_path;
}

} // namespace lcero::common
