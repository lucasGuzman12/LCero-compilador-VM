#include "lcero/lexer/token.hpp"

namespace lcero::lexer {

const char* token_type_name(const TokenType type) noexcept {
    switch (type) {
    case TokenType::KeywordInt:
        return "KW_INT";
    case TokenType::KeywordIf:
        return "KW_IF";
    case TokenType::KeywordElse:
        return "KW_ELSE";
    case TokenType::KeywordWhile:
        return "KW_WHILE";
    case TokenType::KeywordPrint:
        return "KW_PRINT";
    case TokenType::Identifier:
        return "ID";
    case TokenType::Number:
        return "NUMBER";
    case TokenType::Plus:
        return "PLUS";
    case TokenType::Minus:
        return "MINUS";
    case TokenType::Star:
        return "STAR";
    case TokenType::Slash:
        return "SLASH";
    case TokenType::EqualEqual:
        return "EQ";
    case TokenType::BangEqual:
        return "NE";
    case TokenType::Less:
        return "LT";
    case TokenType::Greater:
        return "GT";
    case TokenType::LessEqual:
        return "LE";
    case TokenType::GreaterEqual:
        return "GE";
    case TokenType::Assign:
        return "ASSIGN";
    case TokenType::LeftParen:
        return "LPAREN";
    case TokenType::RightParen:
        return "RPAREN";
    case TokenType::LeftBrace:
        return "LBRACE";
    case TokenType::RightBrace:
        return "RBRACE";
    case TokenType::Semicolon:
        return "SEMICOLON";
    case TokenType::Whitespace:
        return "WHITESPACE";
    case TokenType::EndOfFile:
        return "EOF";
    }

    return "UNKNOWN";
}

bool is_emitted_token(const TokenType type) noexcept {
    return type != TokenType::Whitespace;
}

} // namespace lcero::lexer
