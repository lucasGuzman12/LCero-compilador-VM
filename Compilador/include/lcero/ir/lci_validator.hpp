#pragma once

#include "lcero/common/diagnostic.hpp"

#include <filesystem>
#include <string_view>

namespace lcero::ir {

// Valida la gramatica superficial congelada de LCERO_IR 1. No ejecuta el
// programa ni reemplaza al cargador de la VM independiente.
class LciValidator {
public:
    [[nodiscard]] bool validate(
        std::string_view text,
        const std::filesystem::path& output_path,
        common::DiagnosticBag& diagnostics) const;
};

} // namespace lcero::ir
