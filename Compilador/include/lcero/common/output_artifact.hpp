#pragma once

#include "lcero/common/diagnostic.hpp"

#include <filesystem>
#include <string_view>

namespace lcero::common {

// Administra el artefacto final de manera transaccional. El compilador invalida
// primero cualquier salida anterior y, cuando todas las fases hayan terminado,
// publica un archivo temporal mediante un renombrado en el mismo directorio.
// El contenido se publica solo despues de superar todas las validaciones.
class OutputArtifact {
public:
    [[nodiscard]] bool invalidate_previous(
        const std::filesystem::path& output_path,
        DiagnosticBag& diagnostics) const;

    [[nodiscard]] bool publish(
        const std::filesystem::path& output_path,
        std::string_view content,
        DiagnosticBag& diagnostics) const;

    [[nodiscard]] static std::filesystem::path temporary_path_for(
        const std::filesystem::path& output_path);
};

} // namespace lcero::common
