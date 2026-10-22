#pragma once

#include "lcero/common/diagnostic.hpp"
#include "lcero/ir/ir.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace lcero::ir {

struct BackendResult {
    bool succeeded = false;
    std::string text;
    std::size_t memory_slot_count = 0;
    std::size_t physical_instruction_count = 0;
    std::size_t load_count = 0;
    std::size_t store_count = 0;
    std::size_t cfg_block_count = 0;
    std::size_t cfg_edge_count = 0;
    std::size_t interference_vertex_count = 0;
    std::size_t interference_edge_count = 0;
    std::size_t registers_used = 0;
    std::size_t spilled_temporary_count = 0;
    std::size_t liveness_iterations = 0;
};

// Backend deliberadamente conservador: cada temporal virtual ocupa un slot y
// R0, R1 y R2 se usan solo para materializar una instruccion a la vez.
class ReferenceBackend {
public:
    [[nodiscard]] BackendResult emit(
        const Program& program,
        const std::filesystem::path& output_path,
        common::DiagnosticBag& diagnostics) const;
};

} // namespace lcero::ir
