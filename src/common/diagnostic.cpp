#include "lcero/common/diagnostic.hpp"

#include <algorithm>
#include <utility>

namespace lcero::common {

void DiagnosticBag::report(
    const DiagnosticCategory category,
    const DiagnosticSeverity severity,
    std::string message,
    std::optional<SourceLocation> location) {
    diagnostics_.push_back(Diagnostic{
        category,
        severity,
        std::move(message),
        std::move(location)
    });
}

bool DiagnosticBag::has_errors() const noexcept {
    return std::any_of(
        diagnostics_.begin(),
        diagnostics_.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        });
}

const std::vector<Diagnostic>& DiagnosticBag::diagnostics() const noexcept {
    return diagnostics_;
}

const char* category_name(const DiagnosticCategory category) noexcept {
    switch (category) {
    case DiagnosticCategory::InputOutput:
        return "entrada/salida";
    case DiagnosticCategory::Lexical:
        return "lexico";
    case DiagnosticCategory::Syntactic:
        return "sintactico";
    case DiagnosticCategory::Semantic:
        return "semantico";
    case DiagnosticCategory::Internal:
        return "interno";
    case DiagnosticCategory::CodeGeneration:
        return "generacion";
    case DiagnosticCategory::CommandLine:
        return "argumentos";
    }

    return "desconocido";
}

const char* severity_name(const DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::Information:
        return "info";
    case DiagnosticSeverity::Warning:
        return "advertencia";
    case DiagnosticSeverity::Error:
        return "error";
    }

    return "desconocido";
}

} // namespace lcero::common
