#include "lcero/common/path_resolver.hpp"

#include <system_error>

namespace lcero::common {
namespace {

constexpr const char* project_marker = "LCero-Compilador.vcxproj";

} // namespace

std::optional<std::filesystem::path> PathResolver::find_project_root(
    const std::filesystem::path& current_directory,
    const std::filesystem::path& executable_path) {
    if (const auto from_current = search_upwards(current_directory)) {
        return from_current;
    }

    if (!executable_path.empty()) {
        if (const auto from_executable = search_upwards(executable_path.parent_path())) {
            return from_executable;
        }
    }

    return std::nullopt;
}

std::filesystem::path PathResolver::resolve_from(
    const std::filesystem::path& base,
    const std::filesystem::path& path) {
    if (path.is_absolute()) {
        return path.lexically_normal();
    }

    return (base / path).lexically_normal();
}

std::optional<std::filesystem::path> PathResolver::search_upwards(
    std::filesystem::path start) {
    if (start.empty()) {
        return std::nullopt;
    }

    std::error_code error;
    start = std::filesystem::absolute(start, error);
    if (error) {
        return std::nullopt;
    }

    if (!std::filesystem::is_directory(start, error)) {
        start = start.parent_path();
    }

    while (!start.empty()) {
        error.clear();
        if (std::filesystem::is_regular_file(start / project_marker, error) && !error) {
            return start.lexically_normal();
        }

        const auto parent = start.parent_path();
        if (parent == start) {
            break;
        }
        start = parent;
    }

    return std::nullopt;
}

} // namespace lcero::common
