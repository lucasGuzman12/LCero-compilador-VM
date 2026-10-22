#pragma once

#include "lcero/vm/diagnostics.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lcero::vm {

struct SourceLine {
    std::size_t number;
    std::string text;
};

struct IrSource {
    std::vector<SourceLine> lines;
};

// La cota protege al proceso frente a entradas fisicamente desproporcionadas.
// Superarla es ERROR_RECURSO, no un supuesto error gramatical de LCERO_IR 1.
inline constexpr std::uintmax_t kMaximumIrFileSizeBytes =
    16U * 1024U * 1024U;

// El loader solo abre, limita, lee y normaliza LF/CRLF. El reconocimiento de la
// gramatica de RI pertenece al validador y no se mezcla con E/S fisica.
[[nodiscard]] Result<IrSource> load_ir_source(
    const std::filesystem::path& path);

}  // namespace lcero::vm
