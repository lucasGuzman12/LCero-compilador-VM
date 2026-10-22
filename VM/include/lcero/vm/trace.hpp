#pragma once

#include "lcero/vm/diagnostics.hpp"
#include "lcero/vm/execution_state.hpp"
#include "lcero/vm/ir_validator.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>

namespace lcero::vm {

// TraceWriter es un observador de diagnostico. No modifica C, no escribe en
// stdout y su formato no es LCERO_IR: es un log UTF-8 de diferencias de estado.
class TraceWriter {
public:
    [[nodiscard]] static Result<TraceWriter> open(
        const std::filesystem::path& path);

    TraceWriter(TraceWriter&&) noexcept = default;
    TraceWriter& operator=(TraceWriter&&) noexcept = default;
    TraceWriter(const TraceWriter&) = delete;
    TraceWriter& operator=(const TraceWriter&) = delete;

    void write_header(const ValidatedIr& validated_ir);
    void write_before(const ExecutionState& state,
                      const Instruction& instruction);
    void write_after(const ExecutionState& state,
                     const Instruction& instruction,
                     std::size_t previous_pc,
                     std::uint64_t previous_steps);
    void finalize(const ExecutionState& state);

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::optional<Diagnostic>& diagnostic() const noexcept;

private:
    explicit TraceWriter(std::ofstream output);

    void write_text(const std::string& text);
    void record_failure(DiagnosticCode code, std::string message);

    std::ofstream output_;
    std::optional<Diagnostic> diagnostic_;
    bool finalized_ = false;
};

}  // namespace lcero::vm
