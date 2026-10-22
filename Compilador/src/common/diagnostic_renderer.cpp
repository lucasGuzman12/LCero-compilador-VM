#include "lcero/common/diagnostic_renderer.hpp"

#include <ostream>

namespace lcero::common {

void DiagnosticRenderer::render(
    const Diagnostic& diagnostic,
    std::ostream& output) const {
    output << '[' << severity_name(diagnostic.severity) << ']'
           << '[' << category_name(diagnostic.category) << "] ";

    if (diagnostic.location.has_value()) {
        const auto& location = diagnostic.location.value();
        output << location.file.string();
        if (location.line != 0) {
            output << ':' << location.line;
            if (location.column != 0) {
                output << ':' << location.column;
            }
        }
        output << ": ";
    }

    output << diagnostic.message << '\n';
}

void DiagnosticRenderer::render(
    const DiagnosticBag& diagnostics,
    std::ostream& output) const {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        render(diagnostic, output);
    }
}

} // namespace lcero::common
