#pragma once

#include "lcero/common/diagnostic.hpp"

#include <iosfwd>

namespace lcero::common {

class DiagnosticRenderer {
public:
    void render(const Diagnostic& diagnostic, std::ostream& output) const;
    void render(const DiagnosticBag& diagnostics, std::ostream& output) const;
};

} // namespace lcero::common
