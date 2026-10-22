#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lcero::common {

enum class DiagnosticCategory {
    InputOutput,
    Lexical,
    Syntactic,
    Semantic,
    Internal,
    CodeGeneration,
    CommandLine
};

enum class DiagnosticSeverity {
    Information,
    Warning,
    Error
};

struct SourceLocation {
    std::filesystem::path file;
    std::size_t line = 0;
    std::size_t column = 0;
};

struct Diagnostic {
    DiagnosticCategory category;
    DiagnosticSeverity severity;
    std::string message;
    std::optional<SourceLocation> location;
};

class DiagnosticBag {
public:
    void report(
        DiagnosticCategory category,
        DiagnosticSeverity severity,
        std::string message,
        std::optional<SourceLocation> location = std::nullopt);

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept;

private:
    std::vector<Diagnostic> diagnostics_;
};

[[nodiscard]] const char* category_name(DiagnosticCategory category) noexcept;
[[nodiscard]] const char* severity_name(DiagnosticSeverity severity) noexcept;

} // namespace lcero::common
