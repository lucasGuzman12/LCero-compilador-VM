#include "lcero/lexer/lexer.hpp"

#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace lcero::lexer {
namespace {

struct Position {
    std::size_t line = 1;
    std::size_t column = 1;
};

void advance_position(Position& position, const std::string_view text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        const unsigned char symbol = static_cast<unsigned char>(text[index]);
        if (symbol == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            ++position.line;
            position.column = 1;
        } else if (symbol == '\n') {
            ++position.line;
            position.column = 1;
        } else {
            ++position.column;
        }
    }
}

std::string invalid_symbol_message(const unsigned char symbol) {
    if (symbol == '!') {
        return "El caracter '!' aislado no es un token valido; solo se admite '!='.";
    }

    if (symbol >= 32 && symbol <= 126) {
        return "Caracter fuera del alfabeto lexico: '" +
            std::string(1, static_cast<char>(symbol)) + "'.";
    }

    std::ostringstream message;
    message << "Byte fuera del alfabeto lexico ASCII: 0x"
            << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(symbol) << '.';
    return message.str();
}

} // namespace

Lexer::Lexer() = default;

LexResult Lexer::tokenize(
    const common::SourceFile& source,
    common::DiagnosticBag& diagnostics) const {
    LexResult result;
    const Dfa& dfa = automata_.minimized_dfa();
    const std::string& input = source.content;

    std::size_t offset = 0;
    Position position;

    while (offset < input.size()) {
        const std::size_t token_line = position.line;
        const std::size_t token_column = position.column;
        StateId state = dfa.start_state();
        std::size_t cursor = offset;
        std::optional<std::size_t> last_accepting_offset;
        std::optional<TokenType> last_accepting_type;

        // Maximal munch: se conserva el último prefijo aceptado. El recorrido
        // se detiene antes de entrar al sumidero; si una extensión falla, se
        // retrocede lógicamente a la última aceptación sin releer el lexema.
        while (cursor < input.size()) {
            const unsigned char symbol =
                static_cast<unsigned char>(input[cursor]);
            const auto target = dfa.transition(state, symbol);
            if (!target.has_value() || target.value() == dfa.sink_state()) {
                break;
            }

            state = target.value();
            ++cursor;
            const auto& acceptance = dfa.states()[state].acceptance;
            if (acceptance.has_value()) {
                last_accepting_offset = cursor;
                last_accepting_type = acceptance->token_type;
            }
        }

        if (!last_accepting_offset.has_value() ||
            !last_accepting_type.has_value()) {
            const unsigned char invalid =
                static_cast<unsigned char>(input[offset]);
            diagnostics.report(
                common::DiagnosticCategory::Lexical,
                common::DiagnosticSeverity::Error,
                invalid_symbol_message(invalid),
                common::SourceLocation{
                    source.path,
                    token_line,
                    token_column
                });
            return result;
        }

        const std::size_t length = last_accepting_offset.value() - offset;
        const std::string lexeme = input.substr(offset, length);
        const TokenType type = last_accepting_type.value();

        if (is_emitted_token(type)) {
            result.tokens.push_back(Token{
                type,
                lexeme,
                token_line,
                token_column,
                offset
            });
        }

        advance_position(position, lexeme);
        offset = last_accepting_offset.value();
    }

    result.tokens.push_back(Token{
        TokenType::EndOfFile,
        "",
        position.line,
        position.column,
        offset
    });
    result.succeeded = true;
    return result;
}

const LexerAutomata& Lexer::automata() const noexcept {
    return automata_;
}

} // namespace lcero::lexer
