#pragma once

#include <filesystem>
#include <optional>

namespace lcero::common {

class PathResolver {
public:
    [[nodiscard]] static std::optional<std::filesystem::path> find_project_root(
        const std::filesystem::path& current_directory,
        const std::filesystem::path& executable_path);

    [[nodiscard]] static std::filesystem::path resolve_from(
        const std::filesystem::path& base,
        const std::filesystem::path& path);

private:
    [[nodiscard]] static std::optional<std::filesystem::path> search_upwards(
        std::filesystem::path start);
};

} // namespace lcero::common
