#pragma once

#include "lcero/vm/diagnostics.hpp"
#include "lcero/vm/execution_state.hpp"
#include "lcero/vm/ir_validator.hpp"

#include <cstdint>
#include <optional>

namespace lcero::vm {

class TraceWriter;

inline constexpr std::uint64_t kDefaultMaxSteps = 100000;

struct ExecutionOptions {
    // Un optional vacio representa la decision explicita --no-step-limit. Un
    // cero presente representa un presupuesto matematico N=0 y limita antes
    // de ejecutar la primera instruccion; ambos casos son distintos.
    std::optional<std::uint64_t> max_steps = kDefaultMaxSteps;
    // Puntero no propietario: la CLI conserva el archivo abierto durante toda
    // la ejecucion. Un nullptr mantiene exactamente el camino sin observador.
    TraceWriter* trace_writer = nullptr;
};

// Interpreta la secuencia P ya validada. Los errores dinamicos forman un
// ExecutionState terminal ERROR; Result::failure queda reservado para fallos
// internos o fisicos que no pertenecen a la semantica del programa L-Cero.
[[nodiscard]] Result<ExecutionState> interpret(
    ValidatedIr validated_ir,
    const ExecutionOptions& options = {});

}  // namespace lcero::vm
