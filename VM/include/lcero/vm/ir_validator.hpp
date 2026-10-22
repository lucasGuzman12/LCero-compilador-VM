#pragma once

#include "lcero/vm/diagnostics.hpp"
#include "lcero/vm/instruction.hpp"
#include "lcero/vm/ir_loader.hpp"

#include <cstddef>
#include <map>
#include <string>

namespace lcero::vm {

struct ValidatedIr {
    Program program;
    MemorySize memory_size;
    // LABEL no entra en P, pero la tabla simbolica se conserva como metadato
    // de depuracion para relacionar nombres con indices ejecutables 0-based.
    std::map<std::string, std::size_t> labels;
};

// El validador reconoce LCERO_IR 1 en dos pasadas. Su resultado contiene P con
// saltos resueltos, pero no crea ExecutionState ni ejecuta una instruccion.
[[nodiscard]] Result<ValidatedIr> validate_ir(const IrSource& source);

}  // namespace lcero::vm
