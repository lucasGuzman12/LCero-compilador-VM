#pragma once

#include "lcero/compiler_options.hpp"

#include <filesystem>
#include <istream>
#include <optional>
#include <ostream>

namespace lcero {

enum class MenuAction {
    Compile,
    Exit,
    EndOfInput
};

struct MenuResult {
    MenuAction action = MenuAction::EndOfInput;
    std::optional<BackendKind> backend;
};

class InteractiveMenu {
public:
    [[nodiscard]] MenuResult prompt(
        std::istream& input,
        std::ostream& output,
        const std::filesystem::path& input_path) const;
};

} // namespace lcero
