#pragma once

#include "lcero/common/diagnostic.hpp"
#include "lcero/common/source_reader.hpp"
#include "lcero/lexer/automata.hpp"
#include "lcero/lexer/token.hpp"

#include <vector>

namespace lcero::lexer {

struct LexResult {
    std::vector<Token> tokens;
    bool succeeded = false;
};

class Lexer {
public:
    Lexer();

    [[nodiscard]] LexResult tokenize(
        const common::SourceFile& source,
        common::DiagnosticBag& diagnostics) const;

    [[nodiscard]] const LexerAutomata& automata() const noexcept;

private:
    LexerAutomata automata_;
};

} // namespace lcero::lexer
