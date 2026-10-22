#include "lcero/parser/grammar.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace lcero::parser {
namespace {

using GS = GrammarSymbol;
using TT = lexer::TokenType;
using NT = Nonterminal;

Production make_production(
    const std::size_t number,
    const NT left,
    std::vector<GS> right) {
    return Production{number, left, std::move(right)};
}

} // namespace

GrammarSymbol GrammarSymbol::terminal_symbol(const lexer::TokenType value) {
    GrammarSymbol symbol{};
    symbol.kind = GrammarSymbolKind::Terminal;
    symbol.terminal = value;
    return symbol;
}

GrammarSymbol GrammarSymbol::nonterminal_symbol(const Nonterminal value) {
    GrammarSymbol symbol{};
    symbol.kind = GrammarSymbolKind::Nonterminal;
    symbol.nonterminal = value;
    return symbol;
}

Grammar Grammar::lcero() {
    Grammar grammar;
    grammar.start_symbol_ = NT::Program;
    grammar.nonterminals_ = {
        NT::Program,
        NT::DeclarationSequence,
        NT::Declaration,
        NT::StatementSequence,
        NT::StatementSequenceTail,
        NT::Statement,
        NT::Assignment,
        NT::PrintStatement,
        NT::IfStatement,
        NT::ElseOptional,
        NT::WhileStatement,
        NT::Block,
        NT::Condition,
        NT::Comparator,
        NT::Expression,
        NT::ExpressionTail,
        NT::Term,
        NT::TermTail,
        NT::Unary,
        NT::Primary
    };

    const auto t = GS::terminal_symbol;
    const auto n = GS::nonterminal_symbol;
    grammar.productions_ = {
        make_production(1, NT::Program,
            {n(NT::DeclarationSequence), n(NT::StatementSequence), t(TT::EndOfFile)}),
        make_production(2, NT::DeclarationSequence,
            {n(NT::Declaration), n(NT::DeclarationSequence)}),
        make_production(3, NT::DeclarationSequence, {}),
        make_production(4, NT::Declaration,
            {t(TT::KeywordInt), t(TT::Identifier), t(TT::Semicolon)}),
        make_production(5, NT::StatementSequence,
            {n(NT::Statement), n(NT::StatementSequenceTail)}),
        make_production(6, NT::StatementSequenceTail,
            {n(NT::Statement), n(NT::StatementSequenceTail)}),
        make_production(7, NT::StatementSequenceTail, {}),
        make_production(8, NT::Statement, {n(NT::Assignment)}),
        make_production(9, NT::Statement, {n(NT::PrintStatement)}),
        make_production(10, NT::Statement, {n(NT::IfStatement)}),
        make_production(11, NT::Statement, {n(NT::WhileStatement)}),
        make_production(12, NT::Assignment,
            {t(TT::Identifier), t(TT::Assign), n(NT::Expression), t(TT::Semicolon)}),
        make_production(13, NT::PrintStatement,
            {t(TT::KeywordPrint), t(TT::LeftParen), n(NT::Expression),
             t(TT::RightParen), t(TT::Semicolon)}),
        make_production(14, NT::IfStatement,
            {t(TT::KeywordIf), t(TT::LeftParen), n(NT::Condition),
             t(TT::RightParen), n(NT::Block), n(NT::ElseOptional)}),
        make_production(15, NT::ElseOptional,
            {t(TT::KeywordElse), n(NT::Block)}),
        make_production(16, NT::ElseOptional, {}),
        make_production(17, NT::WhileStatement,
            {t(TT::KeywordWhile), t(TT::LeftParen), n(NT::Condition),
             t(TT::RightParen), n(NT::Block)}),
        make_production(18, NT::Block,
            {t(TT::LeftBrace), n(NT::StatementSequence), t(TT::RightBrace)}),
        make_production(19, NT::Condition,
            {n(NT::Expression), n(NT::Comparator), n(NT::Expression)}),
        make_production(20, NT::Comparator, {t(TT::EqualEqual)}),
        make_production(21, NT::Comparator, {t(TT::BangEqual)}),
        make_production(22, NT::Comparator, {t(TT::Less)}),
        make_production(23, NT::Comparator, {t(TT::Greater)}),
        make_production(24, NT::Comparator, {t(TT::LessEqual)}),
        make_production(25, NT::Comparator, {t(TT::GreaterEqual)}),
        make_production(26, NT::Expression,
            {n(NT::Term), n(NT::ExpressionTail)}),
        make_production(27, NT::ExpressionTail,
            {t(TT::Plus), n(NT::Term), n(NT::ExpressionTail)}),
        make_production(28, NT::ExpressionTail,
            {t(TT::Minus), n(NT::Term), n(NT::ExpressionTail)}),
        make_production(29, NT::ExpressionTail, {}),
        make_production(30, NT::Term,
            {n(NT::Unary), n(NT::TermTail)}),
        make_production(31, NT::TermTail,
            {t(TT::Star), n(NT::Unary), n(NT::TermTail)}),
        make_production(32, NT::TermTail,
            {t(TT::Slash), n(NT::Unary), n(NT::TermTail)}),
        make_production(33, NT::TermTail, {}),
        make_production(34, NT::Unary,
            {t(TT::Minus), n(NT::Unary)}),
        make_production(35, NT::Unary, {n(NT::Primary)}),
        make_production(36, NT::Primary, {t(TT::Number)}),
        make_production(37, NT::Primary, {t(TT::Identifier)}),
        make_production(38, NT::Primary,
            {t(TT::LeftParen), n(NT::Expression), t(TT::RightParen)})
    };

    for (const auto& production : grammar.productions_) {
        for (const auto& symbol : production.right) {
            if (symbol.kind == GrammarSymbolKind::Terminal) {
                grammar.terminals_.insert(symbol.terminal);
            }
        }
    }
    return grammar;
}

Nonterminal Grammar::start_symbol() const noexcept {
    return start_symbol_;
}

const std::vector<Nonterminal>& Grammar::nonterminals() const noexcept {
    return nonterminals_;
}

const std::set<lexer::TokenType>& Grammar::terminals() const noexcept {
    return terminals_;
}

const std::vector<Production>& Grammar::productions() const noexcept {
    return productions_;
}

const Production& Grammar::production(const std::size_t number) const {
    if (number == 0 || number > productions_.size() ||
        productions_[number - 1].number != number) {
        throw std::out_of_range("Numero de produccion invalido.");
    }
    return productions_[number - 1];
}

GrammarAnalysis::GrammarAnalysis(Grammar grammar)
    : grammar_(std::move(grammar)) {
    for (const auto nonterminal : grammar_.nonterminals()) {
        first_[nonterminal];
        follow_[nonterminal];
        table_[nonterminal];
    }
    compute_nullable();
    compute_first();
    compute_follow();
    compute_select_and_table();
}

const Grammar& GrammarAnalysis::grammar() const noexcept {
    return grammar_;
}

bool GrammarAnalysis::nullable(const Nonterminal nonterminal) const {
    return nullable_.count(nonterminal) != 0;
}

const std::set<lexer::TokenType>& GrammarAnalysis::first(
    const Nonterminal nonterminal) const {
    return first_.at(nonterminal);
}

const std::set<lexer::TokenType>& GrammarAnalysis::follow(
    const Nonterminal nonterminal) const {
    return follow_.at(nonterminal);
}

const std::set<lexer::TokenType>& GrammarAnalysis::select(
    const std::size_t production_number) const {
    return select_.at(production_number);
}

std::optional<std::size_t> GrammarAnalysis::production_for(
    const Nonterminal nonterminal,
    const lexer::TokenType lookahead) const {
    const auto row = table_.find(nonterminal);
    if (row == table_.end()) {
        return std::nullopt;
    }
    const auto cell = row->second.find(lookahead);
    return cell == row->second.end()
        ? std::nullopt
        : std::optional<std::size_t>(cell->second);
}

std::set<lexer::TokenType> GrammarAnalysis::expected_tokens(
    const Nonterminal nonterminal) const {
    std::set<lexer::TokenType> result;
    for (const auto& cell : table_.at(nonterminal)) {
        result.insert(cell.first);
    }
    return result;
}

const std::vector<ParseTableConflict>& GrammarAnalysis::conflicts() const noexcept {
    return conflicts_;
}

bool GrammarAnalysis::is_ll1() const noexcept {
    return conflicts_.empty();
}

std::size_t GrammarAnalysis::nullable_pass_count() const noexcept {
    return nullable_pass_count_;
}

std::size_t GrammarAnalysis::first_pass_count() const noexcept {
    return first_pass_count_;
}

std::size_t GrammarAnalysis::follow_pass_count() const noexcept {
    return follow_pass_count_;
}

GrammarAnalysis::SequenceFirst GrammarAnalysis::first_of_sequence(
    const std::vector<GrammarSymbol>& symbols) const {
    SequenceFirst result;
    for (const auto& symbol : symbols) {
        if (symbol.kind == GrammarSymbolKind::Terminal) {
            result.terminals.insert(symbol.terminal);
            result.nullable = false;
            return result;
        }

        const auto& nested_first = first_.at(symbol.nonterminal);
        result.terminals.insert(nested_first.begin(), nested_first.end());
        if (!nullable(symbol.nonterminal)) {
            result.nullable = false;
            return result;
        }
    }
    return result;
}

void GrammarAnalysis::compute_nullable() {
    // Punto fijo de NULLABLE: A se incorpora cuando existe A -> X1...Xn y
    // todos los Xi ya son anulables; la parte derecha vacia cumple la condicion.
    bool changed = true;
    while (changed) {
        ++nullable_pass_count_;
        changed = false;
        for (const auto& production : grammar_.productions()) {
            bool right_nullable = true;
            for (const auto& symbol : production.right) {
                if (symbol.kind == GrammarSymbolKind::Terminal ||
                    !nullable(symbol.nonterminal)) {
                    right_nullable = false;
                    break;
                }
            }
            if (right_nullable && nullable_.insert(production.left).second) {
                changed = true;
            }
        }
    }
}

void GrammarAnalysis::compute_first() {
    // FIRST(A) recibe FIRST de cada simbolo de la derecha hasta encontrar el
    // primero no anulable. La union monotona termina porque T es finito.
    bool changed = true;
    while (changed) {
        ++first_pass_count_;
        changed = false;
        for (const auto& production : grammar_.productions()) {
            const auto sequence_first = first_of_sequence(production.right);
            auto& target = first_[production.left];
            const auto old_size = target.size();
            target.insert(
                sequence_first.terminals.begin(),
                sequence_first.terminals.end());
            changed = changed || target.size() != old_size;
        }
    }
}

void GrammarAnalysis::compute_follow() {
    // Regla formal: en A -> alfa B beta se agrega FIRST(beta) a FOLLOW(B) y,
    // cuando beta =>* epsilon, tambien FOLLOW(A). EOF inicializa al axioma.
    follow_[grammar_.start_symbol()].insert(lexer::TokenType::EndOfFile);

    bool changed = true;
    while (changed) {
        ++follow_pass_count_;
        changed = false;
        for (const auto& production : grammar_.productions()) {
            for (std::size_t index = 0; index < production.right.size(); ++index) {
                const auto& symbol = production.right[index];
                if (symbol.kind != GrammarSymbolKind::Nonterminal) {
                    continue;
                }

                std::vector<GrammarSymbol> suffix(
                    production.right.begin() + static_cast<std::ptrdiff_t>(index + 1),
                    production.right.end());
                const auto suffix_first = first_of_sequence(suffix);
                auto& target = follow_[symbol.nonterminal];
                const auto before_first = target.size();
                target.insert(
                    suffix_first.terminals.begin(),
                    suffix_first.terminals.end());
                changed = changed || target.size() != before_first;

                if (suffix_first.nullable) {
                    const auto before_follow = target.size();
                    const auto& propagated = follow_[production.left];
                    target.insert(propagated.begin(), propagated.end());
                    changed = changed || target.size() != before_follow;
                }
            }
        }
    }
}

void GrammarAnalysis::compute_select_and_table() {
    // SELECT(A -> alfa) = FIRST(alfa), unido con FOLLOW(A) si alfa es anulable.
    // Insertar dos producciones en M[A,a] constituye exactamente un conflicto LL(1).
    for (const auto& production : grammar_.productions()) {
        const auto sequence_first = first_of_sequence(production.right);
        auto selected = sequence_first.terminals;
        if (sequence_first.nullable) {
            const auto& propagated = follow_[production.left];
            selected.insert(propagated.begin(), propagated.end());
        }
        select_[production.number] = selected;

        for (const auto token : selected) {
            auto& cell = table_[production.left];
            const auto inserted = cell.emplace(token, production.number);
            if (!inserted.second) {
                conflicts_.push_back(ParseTableConflict{
                    production.left,
                    token,
                    inserted.first->second,
                    production.number
                });
            }
        }
    }
}

const char* nonterminal_name(const Nonterminal nonterminal) noexcept {
    switch (nonterminal) {
    case Nonterminal::Program: return "Programa";
    case Nonterminal::DeclarationSequence: return "DeclSeq";
    case Nonterminal::Declaration: return "Declaracion";
    case Nonterminal::StatementSequence: return "StmtSeq";
    case Nonterminal::StatementSequenceTail: return "StmtSeqTail";
    case Nonterminal::Statement: return "Instruccion";
    case Nonterminal::Assignment: return "Asignacion";
    case Nonterminal::PrintStatement: return "Impresion";
    case Nonterminal::IfStatement: return "Condicional";
    case Nonterminal::ElseOptional: return "ElseOpt";
    case Nonterminal::WhileStatement: return "Bucle";
    case Nonterminal::Block: return "Bloque";
    case Nonterminal::Condition: return "Condicion";
    case Nonterminal::Comparator: return "Comparador";
    case Nonterminal::Expression: return "Expresion";
    case Nonterminal::ExpressionTail: return "ExprTail";
    case Nonterminal::Term: return "Termino";
    case Nonterminal::TermTail: return "TermTail";
    case Nonterminal::Unary: return "Unario";
    case Nonterminal::Primary: return "Primario";
    }
    return "Desconocido";
}

std::string symbol_name(const GrammarSymbol& symbol) {
    return symbol.kind == GrammarSymbolKind::Terminal
        ? lexer::token_type_name(symbol.terminal)
        : nonterminal_name(symbol.nonterminal);
}

std::string production_text(const Production& production) {
    std::string result = std::to_string(production.number) + ". " +
        nonterminal_name(production.left) + " ->";
    if (production.right.empty()) {
        return result + " epsilon";
    }
    for (const auto& symbol : production.right) {
        result += " " + symbol_name(symbol);
    }
    return result;
}

} // namespace lcero::parser
