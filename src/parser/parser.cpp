#include "lcero/parser/parser.hpp"

#include <sstream>
#include <string>
#include <utility>

namespace lcero::parser {

Parser::Parser() = default;

ParseResult Parser::parse(
    const std::vector<lexer::Token>& tokens,
    const std::filesystem::path& source_path,
    common::DiagnosticBag& diagnostics) {
    tokens_ = &tokens;
    source_path_ = source_path;
    diagnostics_ = &diagnostics;
    current_ = 0;
    error_reported_ = false;

    if (!grammar_analysis_.is_ll1()) {
        diagnostics.report(
            common::DiagnosticCategory::Internal,
            common::DiagnosticSeverity::Error,
            "La gramatica configurada contiene conflictos LL(1).");
        return {};
    }

    if (tokens.empty()) {
        diagnostics.report(
            common::DiagnosticCategory::Internal,
            common::DiagnosticSeverity::Error,
            "El parser recibio una secuencia sin token EOF.");
        return {};
    }

    try {
        auto program = parse_program();
        if (current_ != tokens.size()) {
            report_expected({lexer::TokenType::EndOfFile}, lookahead());
            throw ParseFailure{};
        }
        return ParseResult{std::move(program), true};
    } catch (const ParseFailure&) {
        return {};
    }
}

const GrammarAnalysis& Parser::grammar_analysis() const noexcept {
    return grammar_analysis_;
}

const lexer::Token& Parser::lookahead() const {
    if (current_ < tokens_->size()) {
        return tokens_->at(current_);
    }
    return tokens_->back();
}

const lexer::Token& Parser::consume(const lexer::TokenType expected) {
    const auto& token = lookahead();
    if (token.type != expected) {
        report_expected({expected}, token);
        throw ParseFailure{};
    }
    ++current_;
    return token;
}

std::size_t Parser::select_production(const Nonterminal nonterminal) {
    // Una unica celda M[no terminal, lookahead] decide la produccion. Una
    // celda vacia significa que ningun SELECT admite el token observado.
    const auto selected = grammar_analysis_.production_for(
        nonterminal,
        lookahead().type);
    if (!selected.has_value()) {
        report_expected(grammar_analysis_.expected_tokens(nonterminal), lookahead());
        throw ParseFailure{};
    }
    return selected.value();
}

void Parser::report_expected(
    const std::set<lexer::TokenType>& expected,
    const lexer::Token& found) {
    if (error_reported_) {
        return;
    }
    error_reported_ = true;

    std::ostringstream message;
    message << "Token inesperado " << lexer::token_type_name(found.type);
    if (!found.lexeme.empty()) {
        message << " ('" << found.lexeme << "')";
    }
    message << "; se esperaba ";

    std::size_t index = 0;
    for (const auto token : expected) {
        if (index != 0) {
            message << (index + 1 == expected.size() ? " o " : ", ");
        }
        message << lexer::token_type_name(token);
        ++index;
    }
    if (expected.empty()) {
        message << "una produccion valida";
    }
    message << '.';

    diagnostics_->report(
        common::DiagnosticCategory::Syntactic,
        common::DiagnosticSeverity::Error,
        message.str(),
        common::SourceLocation{
            source_path_,
            found.line,
            found.column
        });
}

std::unique_ptr<ast::Program> Parser::parse_program() {
    if (select_production(Nonterminal::Program) != 1) {
        throw ParseFailure{};
    }
    const auto program_position = position_of(lookahead());
    auto declarations = parse_declaration_sequence();
    auto statements = parse_statement_sequence();
    static_cast<void>(consume(lexer::TokenType::EndOfFile));
    return std::make_unique<ast::Program>(
        program_position,
        std::move(declarations),
        std::move(statements));
}

std::vector<ast::Declaration> Parser::parse_declaration_sequence() {
    std::vector<ast::Declaration> declarations;
    while (true) {
        const std::size_t production =
            select_production(Nonterminal::DeclarationSequence);
        if (production == 3) {
            return declarations;
        }
        if (production != 2) {
            throw ParseFailure{};
        }
        declarations.push_back(parse_declaration());
    }
}

ast::Declaration Parser::parse_declaration() {
    if (select_production(Nonterminal::Declaration) != 4) {
        throw ParseFailure{};
    }
    static_cast<void>(consume(lexer::TokenType::KeywordInt));
    const auto& identifier = consume(lexer::TokenType::Identifier);
    static_cast<void>(consume(lexer::TokenType::Semicolon));
    return ast::Declaration(position_of(identifier), identifier.lexeme);
}

std::vector<std::unique_ptr<ast::Statement>>
Parser::parse_statement_sequence() {
    if (select_production(Nonterminal::StatementSequence) != 5) {
        throw ParseFailure{};
    }
    std::vector<std::unique_ptr<ast::Statement>> statements;
    statements.push_back(parse_statement());
    parse_statement_sequence_tail(statements);
    return statements;
}

void Parser::parse_statement_sequence_tail(
    std::vector<std::unique_ptr<ast::Statement>>& statements) {
    while (true) {
        const std::size_t production =
            select_production(Nonterminal::StatementSequenceTail);
        if (production == 7) {
            return;
        }
        if (production != 6) {
            throw ParseFailure{};
        }
        statements.push_back(parse_statement());
    }
}

std::unique_ptr<ast::Statement> Parser::parse_statement() {
    switch (select_production(Nonterminal::Statement)) {
    case 8: return parse_assignment();
    case 9: return parse_print_statement();
    case 10: return parse_if_statement();
    case 11: return parse_while_statement();
    default: throw ParseFailure{};
    }
}

std::unique_ptr<ast::Statement> Parser::parse_assignment() {
    if (select_production(Nonterminal::Assignment) != 12) {
        throw ParseFailure{};
    }
    const auto& identifier = consume(lexer::TokenType::Identifier);
    static_cast<void>(consume(lexer::TokenType::Assign));
    auto expression = parse_expression();
    static_cast<void>(consume(lexer::TokenType::Semicolon));
    return std::make_unique<ast::AssignmentStatement>(
        position_of(identifier),
        identifier.lexeme,
        std::move(expression));
}

std::unique_ptr<ast::Statement> Parser::parse_print_statement() {
    if (select_production(Nonterminal::PrintStatement) != 13) {
        throw ParseFailure{};
    }
    const auto& keyword = consume(lexer::TokenType::KeywordPrint);
    static_cast<void>(consume(lexer::TokenType::LeftParen));
    auto expression = parse_expression();
    static_cast<void>(consume(lexer::TokenType::RightParen));
    static_cast<void>(consume(lexer::TokenType::Semicolon));
    return std::make_unique<ast::PrintStatement>(
        position_of(keyword),
        std::move(expression));
}

std::unique_ptr<ast::Statement> Parser::parse_if_statement() {
    if (select_production(Nonterminal::IfStatement) != 14) {
        throw ParseFailure{};
    }
    const auto& keyword = consume(lexer::TokenType::KeywordIf);
    static_cast<void>(consume(lexer::TokenType::LeftParen));
    auto condition = parse_condition();
    static_cast<void>(consume(lexer::TokenType::RightParen));
    auto then_block = parse_block();
    auto else_block = parse_else_optional();
    return std::make_unique<ast::IfStatement>(
        position_of(keyword),
        std::move(condition),
        std::move(then_block),
        std::move(else_block));
}

std::unique_ptr<ast::Block> Parser::parse_else_optional() {
    const std::size_t production = select_production(Nonterminal::ElseOptional);
    if (production == 16) {
        return nullptr;
    }
    if (production != 15) {
        throw ParseFailure{};
    }
    static_cast<void>(consume(lexer::TokenType::KeywordElse));
    return parse_block();
}

std::unique_ptr<ast::Statement> Parser::parse_while_statement() {
    if (select_production(Nonterminal::WhileStatement) != 17) {
        throw ParseFailure{};
    }
    const auto& keyword = consume(lexer::TokenType::KeywordWhile);
    static_cast<void>(consume(lexer::TokenType::LeftParen));
    auto condition = parse_condition();
    static_cast<void>(consume(lexer::TokenType::RightParen));
    auto body = parse_block();
    return std::make_unique<ast::WhileStatement>(
        position_of(keyword),
        std::move(condition),
        std::move(body));
}

std::unique_ptr<ast::Block> Parser::parse_block() {
    if (select_production(Nonterminal::Block) != 18) {
        throw ParseFailure{};
    }
    const auto& brace = consume(lexer::TokenType::LeftBrace);
    auto statements = parse_statement_sequence();
    static_cast<void>(consume(lexer::TokenType::RightBrace));
    return std::make_unique<ast::Block>(
        position_of(brace),
        std::move(statements));
}

std::unique_ptr<ast::Condition> Parser::parse_condition() {
    if (select_production(Nonterminal::Condition) != 19) {
        throw ParseFailure{};
    }
    auto left = parse_expression();
    const auto comparator_token = lookahead();
    const auto operation = parse_comparator();
    auto right = parse_expression();
    return std::make_unique<ast::Condition>(
        position_of(comparator_token),
        operation,
        std::move(left),
        std::move(right));
}

ast::ComparisonOperator Parser::parse_comparator() {
    switch (select_production(Nonterminal::Comparator)) {
    case 20:
        static_cast<void>(consume(lexer::TokenType::EqualEqual));
        return ast::ComparisonOperator::Equal;
    case 21:
        static_cast<void>(consume(lexer::TokenType::BangEqual));
        return ast::ComparisonOperator::NotEqual;
    case 22:
        static_cast<void>(consume(lexer::TokenType::Less));
        return ast::ComparisonOperator::Less;
    case 23:
        static_cast<void>(consume(lexer::TokenType::Greater));
        return ast::ComparisonOperator::Greater;
    case 24:
        static_cast<void>(consume(lexer::TokenType::LessEqual));
        return ast::ComparisonOperator::LessEqual;
    case 25:
        static_cast<void>(consume(lexer::TokenType::GreaterEqual));
        return ast::ComparisonOperator::GreaterEqual;
    default:
        throw ParseFailure{};
    }
}

std::unique_ptr<ast::Expression> Parser::parse_expression() {
    if (select_production(Nonterminal::Expression) != 26) {
        throw ParseFailure{};
    }
    auto left = parse_term();

    // ExprTail es recursiva a derecha en la BNF para ser LL(1), pero el AST se
    // pliega iterativamente a izquierda. Así a-b-c representa (a-b)-c sin
    // reintroducir recursión izquierda en el parser.
    while (true) {
        const std::size_t production =
            select_production(Nonterminal::ExpressionTail);
        if (production == 29) {
            return left;
        }

        lexer::Token operation_token = lookahead();
        ast::BinaryOperator operation;
        if (production == 27) {
            static_cast<void>(consume(lexer::TokenType::Plus));
            operation = ast::BinaryOperator::Add;
        } else if (production == 28) {
            static_cast<void>(consume(lexer::TokenType::Minus));
            operation = ast::BinaryOperator::Subtract;
        } else {
            throw ParseFailure{};
        }

        auto right = parse_term();
        left = std::make_unique<ast::BinaryExpression>(
            position_of(operation_token),
            operation,
            std::move(left),
            std::move(right));
    }
}

std::unique_ptr<ast::Expression> Parser::parse_term() {
    if (select_production(Nonterminal::Term) != 30) {
        throw ParseFailure{};
    }
    auto left = parse_unary();

    while (true) {
        const std::size_t production = select_production(Nonterminal::TermTail);
        if (production == 33) {
            return left;
        }

        lexer::Token operation_token = lookahead();
        ast::BinaryOperator operation;
        if (production == 31) {
            static_cast<void>(consume(lexer::TokenType::Star));
            operation = ast::BinaryOperator::Multiply;
        } else if (production == 32) {
            static_cast<void>(consume(lexer::TokenType::Slash));
            operation = ast::BinaryOperator::Divide;
        } else {
            throw ParseFailure{};
        }

        auto right = parse_unary();
        left = std::make_unique<ast::BinaryExpression>(
            position_of(operation_token),
            operation,
            std::move(left),
            std::move(right));
    }
}

std::unique_ptr<ast::Expression> Parser::parse_unary() {
    switch (select_production(Nonterminal::Unary)) {
    case 34: {
        const auto& minus = consume(lexer::TokenType::Minus);
        auto operand = parse_unary();
        return std::make_unique<ast::UnaryExpression>(
            position_of(minus),
            ast::UnaryOperator::Negate,
            std::move(operand));
    }
    case 35:
        return parse_primary();
    default:
        throw ParseFailure{};
    }
}

std::unique_ptr<ast::Expression> Parser::parse_primary() {
    switch (select_production(Nonterminal::Primary)) {
    case 36: {
        const auto& number = consume(lexer::TokenType::Number);
        return std::make_unique<ast::IntegerLiteral>(
            position_of(number),
            number.lexeme);
    }
    case 37: {
        const auto& identifier = consume(lexer::TokenType::Identifier);
        return std::make_unique<ast::VariableExpression>(
            position_of(identifier),
            identifier.lexeme);
    }
    case 38: {
        static_cast<void>(consume(lexer::TokenType::LeftParen));
        auto expression = parse_expression();
        static_cast<void>(consume(lexer::TokenType::RightParen));
        return expression;
    }
    default:
        throw ParseFailure{};
    }
}

ast::SourcePosition Parser::position_of(const lexer::Token& token) {
    return ast::SourcePosition{token.line, token.column, token.offset};
}

} // namespace lcero::parser
