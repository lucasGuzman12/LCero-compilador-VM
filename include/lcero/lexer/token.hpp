#pragma once

#include <cstddef>
#include <string>

namespace lcero::lexer {

enum class TokenType {
    KeywordInt,
    KeywordIf,
    KeywordElse,
    KeywordWhile,
    KeywordPrint,
    Identifier,
    Number,
    Plus,
    Minus,
    Star,
    Slash,
    EqualEqual,
    BangEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Assign,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Semicolon,
    Whitespace,
    EndOfFile
};

struct Token {
    TokenType type;
    std::string lexeme;
    std::size_t line;
    std::size_t column;
    std::size_t offset;
};

[[nodiscard]] const char* token_type_name(TokenType type) noexcept;
[[nodiscard]] bool is_emitted_token(TokenType type) noexcept;

} // namespace lcero::lexer
