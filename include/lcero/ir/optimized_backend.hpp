#pragma once

#include "lcero/common/diagnostic.hpp"
#include "lcero/ir/ir.hpp"
#include "lcero/ir/reference_backend.hpp"

#include <filesystem>

namespace lcero::ir {

// R0 y R1 son auxiliares exclusivos. Chaitin asigna solamente R2 y R3;
// así una carga de spill nunca destruye un temporal coloreado vivo.
class OptimizedBackend {
public:
    [[nodiscard]] BackendResult emit(
        const Program& program,
        const std::filesystem::path& output_path,
        common::DiagnosticBag& diagnostics) const;
};

} // namespace lcero::ir
