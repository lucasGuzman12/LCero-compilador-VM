#pragma once

#include "lcero/lexer/token.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lcero::parser {

enum class Nonterminal {
    Program,
    DeclarationSequence,
    Declaration,
    StatementSequence,
    StatementSequenceTail,
    Statement,
    Assignment,
    PrintStatement,
    IfStatement,
    ElseOptional,
    WhileStatement,
    Block,
    Condition,
    Comparator,
    Expression,
    ExpressionTail,
    Term,
    TermTail,
    Unary,
    Primary
};

enum class GrammarSymbolKind {
    Terminal,
    Nonterminal
};

struct GrammarSymbol {
    GrammarSymbolKind kind;
    lexer::TokenType terminal = lexer::TokenType::EndOfFile;
    Nonterminal nonterminal = Nonterminal::Program;

    [[nodiscard]] static GrammarSymbol terminal_symbol(lexer::TokenType value);
    [[nodiscard]] static GrammarSymbol nonterminal_symbol(Nonterminal value);
};

struct Production {
    std::size_t number;
    Nonterminal left;
    std::vector<GrammarSymbol> right;
};

class Grammar {
public:
    [[nodiscard]] static Grammar lcero();

    [[nodiscard]] Nonterminal start_symbol() const noexcept;
    [[nodiscard]] const std::vector<Nonterminal>& nonterminals() const noexcept;
    [[nodiscard]] const std::set<lexer::TokenType>& terminals() const noexcept;
    [[nodiscard]] const std::vector<Production>& productions() const noexcept;
    [[nodiscard]] const Production& production(std::size_t number) const;

private:
    Nonterminal start_symbol_ = Nonterminal::Program;
    std::vector<Nonterminal> nonterminals_;
    std::set<lexer::TokenType> terminals_;
    std::vector<Production> productions_;
};

struct ParseTableConflict {
    Nonterminal nonterminal;
    lexer::TokenType lookahead;
    std::size_t existing_production;
    std::size_t conflicting_production;
};

class GrammarAnalysis {
public:
    explicit GrammarAnalysis(Grammar grammar = Grammar::lcero());

    [[nodiscard]] const Grammar& grammar() const noexcept;
    [[nodiscard]] bool nullable(Nonterminal nonterminal) const;
    [[nodiscard]] const std::set<lexer::TokenType>& first(
        Nonterminal nonterminal) const;
    [[nodiscard]] const std::set<lexer::TokenType>& follow(
        Nonterminal nonterminal) const;
    [[nodiscard]] const std::set<lexer::TokenType>& select(
        std::size_t production_number) const;
    [[nodiscard]] std::optional<std::size_t> production_for(
        Nonterminal nonterminal,
        lexer::TokenType lookahead) const;
    [[nodiscard]] std::set<lexer::TokenType> expected_tokens(
        Nonterminal nonterminal) const;
    [[nodiscard]] const std::vector<ParseTableConflict>& conflicts() const noexcept;
    [[nodiscard]] bool is_ll1() const noexcept;
    [[nodiscard]] std::size_t nullable_pass_count() const noexcept;
    [[nodiscard]] std::size_t first_pass_count() const noexcept;
    [[nodiscard]] std::size_t follow_pass_count() const noexcept;

private:
    using TokenSet = std::set<lexer::TokenType>;

    struct SequenceFirst {
        TokenSet terminals;
        bool nullable = true;
    };

    [[nodiscard]] SequenceFirst first_of_sequence(
        const std::vector<GrammarSymbol>& symbols) const;
    void compute_nullable();
    void compute_first();
    void compute_follow();
    void compute_select_and_table();

    Grammar grammar_;
    std::set<Nonterminal> nullable_;
    std::map<Nonterminal, TokenSet> first_;
    std::map<Nonterminal, TokenSet> follow_;
    std::map<std::size_t, TokenSet> select_;
    std::map<Nonterminal, std::map<lexer::TokenType, std::size_t>> table_;
    std::vector<ParseTableConflict> conflicts_;
    std::size_t nullable_pass_count_ = 0;
    std::size_t first_pass_count_ = 0;
    std::size_t follow_pass_count_ = 0;
};

[[nodiscard]] const char* nonterminal_name(Nonterminal nonterminal) noexcept;
[[nodiscard]] std::string symbol_name(const GrammarSymbol& symbol);
[[nodiscard]] std::string production_text(const Production& production);

} // namespace lcero::parser
