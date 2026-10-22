#include "lcero/vm/ir_loader.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace lcero::vm {
namespace {

Diagnostic resource_error(const DiagnosticCode code, std::string message) {
    return {DiagnosticCategory::resource, code, std::move(message)};
}

Diagnostic text_error(const DiagnosticCode code,
                      std::string message,
                      const std::size_t line) {
    return {DiagnosticCategory::load, code, std::move(message), line};
}

Result<IrSource> split_and_validate_bytes(const std::string& bytes) {
    IrSource source;
    std::string current_line;
    std::size_t line_number = 1U;

    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const unsigned char byte =
            static_cast<unsigned char>(bytes[index]);

        if (byte == '\r') {
            // Solo se aceptan los dos finales de linea congelados: LF y CRLF.
            if (index + 1U >= bytes.size() || bytes[index + 1U] != '\n') {
                return Result<IrSource>::failure(text_error(
                    DiagnosticCode::invalid_line_ending,
                    "se encontro CR sin el LF requerido", line_number));
            }
            continue;
        }

        if (byte == '\n') {
            source.lines.push_back(
                SourceLine{line_number, std::move(current_line)});
            current_line.clear();
            ++line_number;
            continue;
        }

        // La RI admite ASCII visible y tabulacion como separador horizontal.
        // NUL, controles, DEL y cualquier byte UTF-8 no ASCII se rechazan aqui.
        if ((byte < 0x20U && byte != '\t') || byte > 0x7EU) {
            return Result<IrSource>::failure(text_error(
                DiagnosticCode::invalid_character,
                "el archivo contiene un byte no permitido por el subconjunto ASCII",
                line_number));
        }

        current_line.push_back(static_cast<char>(byte));
    }

    // Incluso sin LF final existe una ultima linea logica; un archivo vacio no
    // necesita una linea artificial porque el validador detectara el encabezado.
    if (!current_line.empty() ||
        (!bytes.empty() && bytes.back() != '\n')) {
        source.lines.push_back(SourceLine{line_number, std::move(current_line)});
    }

    return Result<IrSource>::success(std::move(source));
}

}  // namespace

Result<IrSource> load_ir_source(const std::filesystem::path& path) {
    try {
        std::error_code size_error;
        const std::uintmax_t file_size =
            std::filesystem::file_size(path, size_error);
        if (size_error) {
            return Result<IrSource>::failure(resource_error(
                DiagnosticCode::file_open_failed,
                "no se pudo consultar el archivo de entrada"));
        }
        if (file_size > kMaximumIrFileSizeBytes) {
            return Result<IrSource>::failure(resource_error(
                DiagnosticCode::file_too_large,
                "el archivo supera el limite fisico documentado de 16 MiB"));
        }

        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            return Result<IrSource>::failure(resource_error(
                DiagnosticCode::file_open_failed,
                "no se pudo abrir el archivo de entrada"));
        }

        std::string bytes;
        bytes.reserve(static_cast<std::size_t>(file_size));
        std::array<char, 8192> buffer{};
        while (input) {
            input.read(buffer.data(),
                       static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0) {
                bytes.append(buffer.data(), static_cast<std::size_t>(count));
            }
        }

        if (!input.eof()) {
            return Result<IrSource>::failure(resource_error(
                DiagnosticCode::file_read_failed,
                "la lectura del archivo termino por un fallo fisico"));
        }

        return split_and_validate_bytes(bytes);
    } catch (const std::bad_alloc&) {
        return Result<IrSource>::failure(resource_error(
            DiagnosticCode::resource_exhausted,
            "memoria insuficiente al cargar el archivo"));
    } catch (const std::length_error&) {
        return Result<IrSource>::failure(resource_error(
            DiagnosticCode::resource_exhausted,
            "el contenido excede la capacidad representable del proceso"));
    } catch (const std::filesystem::filesystem_error&) {
        return Result<IrSource>::failure(resource_error(
            DiagnosticCode::file_read_failed,
            "el sistema de archivos rechazo la operacion de lectura"));
    }
}

}  // namespace lcero::vm
