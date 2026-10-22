#pragma once

#include "lcero/common/diagnostic.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace lcero::common {

struct SourceFile {
    std::filesystem::path path;
    std::string content;
};

class SourceReader {
public:
    [[nodiscard]] std::optional<SourceFile> read(
        const std::filesystem::path& path,
        DiagnosticBag& diagnostics) const;
};

} // namespace lcero::common
