#pragma once

#include "lcero/common/diagnostic.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lcero {

enum class BackendKind {
    Reference,
    Optimized
};

struct CompilerOptions {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    BackendKind backend = BackendKind::Reference;
};

struct CommandLineParseResult {
    std::optional<CompilerOptions> options;
    common::DiagnosticBag diagnostics;
    bool show_help = false;
};

class CommandLineParser {
public:
    [[nodiscard]] CommandLineParseResult parse(
        const std::vector<std::string>& arguments,
        const std::filesystem::path& project_root,
        const std::filesystem::path& current_directory) const;
};

} // namespace lcero
