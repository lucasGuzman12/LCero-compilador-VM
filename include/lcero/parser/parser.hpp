#pragma once

#include "lcero/ast/ast.hpp"
#include "lcero/common/diagnostic.hpp"
#include "lcero/lexer/token.hpp"
#include "lcero/parser/grammar.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace lcero::parser {

struct ParseResult {
    std::unique_ptr<ast::Program> program;
    bool succeeded = false;
};

class Parser {
public:
    Parser();

    [[nodiscard]] ParseResult parse(
        const std::vector<lexer::Token>& tokens,
        const std::filesystem::path& source_path,
        common::DiagnosticBag& diagnostics);

    [[nodiscard]] const GrammarAnalysis& grammar_analysis() const noexcept;

private:
    struct ParseFailure {};

    [[nodiscard]] const lexer::Token& lookahead() const;
    [[nodiscard]] const lexer::Token& consume(lexer::TokenType expected);
    [[nodiscard]] std::size_t select_production(Nonterminal nonterminal);
    void report_expected(
        const std::set<lexer::TokenType>& expected,
        const lexer::Token& found);

    [[nodiscard]] std::unique_ptr<ast::Program> parse_program();
    [[nodiscard]] std::vector<ast::Declaration> parse_declaration_sequence();
    [[nodiscard]] ast::Declaration parse_declaration();
    [[nodiscard]] std::vector<std::unique_ptr<ast::Statement>>
        parse_statement_sequence();
    void parse_statement_sequence_tail(
        std::vector<std::unique_ptr<ast::Statement>>& statements);
    [[nodiscard]] std::unique_ptr<ast::Statement> parse_statement();
    [[nodiscard]] std::unique_ptr<ast::Statement> parse_assignment();
    [[nodiscard]] std::unique_ptr<ast::Statement> parse_print_statement();
    [[nodiscard]] std::unique_ptr<ast::Statement> parse_if_statement();
    [[nodiscard]] std::unique_ptr<ast::Block> parse_else_optional();
    [[nodiscard]] std::unique_ptr<ast::Statement> parse_while_statement();
    [[nodiscard]] std::unique_ptr<ast::Block> parse_block();
    [[nodiscard]] std::unique_ptr<ast::Condition> parse_condition();
    [[nodiscard]] ast::ComparisonOperator parse_comparator();
    [[nodiscard]] std::unique_ptr<ast::Expression> parse_expression();
    [[nodiscard]] std::unique_ptr<ast::Expression> parse_term();
    [[nodiscard]] std::unique_ptr<ast::Expression> parse_unary();
    [[nodiscard]] std::unique_ptr<ast::Expression> parse_primary();

    [[nodiscard]] static ast::SourcePosition position_of(const lexer::Token& token);

    GrammarAnalysis grammar_analysis_;
    const std::vector<lexer::Token>* tokens_ = nullptr;
    std::filesystem::path source_path_;
    common::DiagnosticBag* diagnostics_ = nullptr;
    std::size_t current_ = 0;
    bool error_reported_ = false;
};

} // namespace lcero::parser
