#include "lcero/lexer/automata.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <utility>

namespace lcero::lexer {
namespace {

constexpr int keyword_priority = 0;
constexpr int fixed_token_priority = 10;
constexpr int general_pattern_priority = 100;
constexpr int whitespace_priority = 200;

std::optional<TokenType> observable_acceptance(const DfaState& state) {
    if (!state.acceptance.has_value()) {
        return std::nullopt;
    }
    return state.acceptance->token_type;
}

bool same_observable_acceptance(const DfaState& left, const DfaState& right) {
    return observable_acceptance(left) == observable_acceptance(right);
}

} // namespace

bool operator==(const Acceptance& left, const Acceptance& right) noexcept {
    return left.token_type == right.token_type && left.priority == right.priority;
}

bool operator!=(const Acceptance& left, const Acceptance& right) noexcept {
    return !(left == right);
}

const std::vector<unsigned char>& lexical_alphabet() {
    static const std::vector<unsigned char> alphabet = [] {
        std::vector<unsigned char> symbols;
        for (unsigned char symbol = 'a'; symbol <= 'z'; ++symbol) {
            symbols.push_back(symbol);
        }
        for (unsigned char symbol = 'A'; symbol <= 'Z'; ++symbol) {
            symbols.push_back(symbol);
        }
        for (unsigned char symbol = '0'; symbol <= '9'; ++symbol) {
            symbols.push_back(symbol);
        }

        const std::string operators = "+-*/=!<>";
        const std::string delimiters = "(){};_";
        const std::string whitespace = " \t\r\n";
        for (const unsigned char symbol : operators + delimiters + whitespace) {
            symbols.push_back(symbol);
        }

        std::sort(symbols.begin(), symbols.end());
        symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
        return symbols;
    }();

    return alphabet;
}

bool is_lexical_alphabet_character(const unsigned char symbol) noexcept {
    return (symbol >= 'a' && symbol <= 'z') ||
           (symbol >= 'A' && symbol <= 'Z') ||
           (symbol >= '0' && symbol <= '9') ||
           symbol == '+' || symbol == '-' || symbol == '*' || symbol == '/' ||
           symbol == '=' || symbol == '!' || symbol == '<' || symbol == '>' ||
           symbol == '(' || symbol == ')' || symbol == '{' || symbol == '}' ||
           symbol == ';' || symbol == '_' || symbol == ' ' || symbol == '\t' ||
           symbol == '\r' || symbol == '\n';
}

std::string display_symbol(const unsigned char symbol) {
    switch (symbol) {
    case ' ':
        return "SPACE";
    case '\t':
        return "TAB";
    case '\r':
        return "CR";
    case '\n':
        return "LF";
    default:
        return std::string(1, static_cast<char>(symbol));
    }
}

Nfa::Nfa() {
    start_state_ = add_state();
}

Nfa Nfa::build_lexer_nfa() {
    Nfa nfa;

    // Cada llamada crea una rama independiente unida al inicio por epsilon.
    // Esto implementa la unión regular de los patrones sin usar std::regex ni
    // un generador externo. Las coincidencias se resuelven después al formar
    // subconjuntos del AFN.
    nfa.add_literal("int", TokenType::KeywordInt, keyword_priority);
    nfa.add_literal("if", TokenType::KeywordIf, keyword_priority);
    nfa.add_literal("else", TokenType::KeywordElse, keyword_priority);
    nfa.add_literal("while", TokenType::KeywordWhile, keyword_priority);
    nfa.add_literal("print", TokenType::KeywordPrint, keyword_priority);

    nfa.add_identifier_pattern();
    nfa.add_number_pattern();
    nfa.add_whitespace_pattern();

    nfa.add_literal("+", TokenType::Plus, fixed_token_priority);
    nfa.add_literal("-", TokenType::Minus, fixed_token_priority);
    nfa.add_literal("*", TokenType::Star, fixed_token_priority);
    nfa.add_literal("/", TokenType::Slash, fixed_token_priority);
    nfa.add_literal("==", TokenType::EqualEqual, fixed_token_priority);
    nfa.add_literal("!=", TokenType::BangEqual, fixed_token_priority);
    nfa.add_literal("<", TokenType::Less, fixed_token_priority);
    nfa.add_literal(">", TokenType::Greater, fixed_token_priority);
    nfa.add_literal("<=", TokenType::LessEqual, fixed_token_priority);
    nfa.add_literal(">=", TokenType::GreaterEqual, fixed_token_priority);
    nfa.add_literal("=", TokenType::Assign, fixed_token_priority);
    nfa.add_literal("(", TokenType::LeftParen, fixed_token_priority);
    nfa.add_literal(")", TokenType::RightParen, fixed_token_priority);
    nfa.add_literal("{", TokenType::LeftBrace, fixed_token_priority);
    nfa.add_literal("}", TokenType::RightBrace, fixed_token_priority);
    nfa.add_literal(";", TokenType::Semicolon, fixed_token_priority);

    return nfa;
}

StateId Nfa::start_state() const noexcept {
    return start_state_;
}

std::size_t Nfa::state_count() const noexcept {
    return states_.size();
}

const std::vector<NfaState>& Nfa::states() const noexcept {
    return states_;
}

std::set<StateId> Nfa::epsilon_closure(
    const std::set<StateId>& input_states) const {
    std::set<StateId> closure = input_states;
    std::vector<StateId> pending(input_states.begin(), input_states.end());

    while (!pending.empty()) {
        const StateId state = pending.back();
        pending.pop_back();
        for (const StateId target : states_.at(state).epsilon_transitions) {
            if (closure.insert(target).second) {
                pending.push_back(target);
            }
        }
    }

    return closure;
}

std::set<StateId> Nfa::move(
    const std::set<StateId>& input_states,
    const unsigned char symbol) const {
    std::set<StateId> result;
    if (symbol >= 128) {
        return result;
    }

    for (const StateId state : input_states) {
        for (const StateId target : states_.at(state).transitions[symbol]) {
            result.insert(target);
        }
    }
    return result;
}

std::optional<Acceptance> Nfa::acceptance_of(
    const std::set<StateId>& input_states) const {
    std::optional<Acceptance> selected;
    for (const StateId state : input_states) {
        const auto& candidate = states_.at(state).acceptance;
        if (!candidate.has_value()) {
            continue;
        }
        if (!selected.has_value() || candidate->priority < selected->priority) {
            selected = candidate;
        }
    }
    return selected;
}

std::optional<TokenType> Nfa::recognize(const std::string_view lexeme) const {
    std::set<StateId> current = epsilon_closure({start_state_});
    for (const unsigned char symbol : lexeme) {
        if (!is_lexical_alphabet_character(symbol)) {
            return std::nullopt;
        }
        current = epsilon_closure(move(current, symbol));
        if (current.empty()) {
            return std::nullopt;
        }
    }

    const auto acceptance = acceptance_of(current);
    return acceptance.has_value()
        ? std::optional<TokenType>(acceptance->token_type)
        : std::nullopt;
}

StateId Nfa::add_state() {
    states_.push_back(NfaState{});
    return states_.size() - 1;
}

void Nfa::add_epsilon(const StateId from, const StateId to) {
    states_.at(from).epsilon_transitions.push_back(to);
}

void Nfa::add_transition(
    const StateId from,
    const unsigned char symbol,
    const StateId to) {
    states_.at(from).transitions.at(symbol).push_back(to);
}

void Nfa::mark_accepting(
    const StateId state,
    const TokenType token_type,
    const int priority) {
    states_.at(state).acceptance = Acceptance{token_type, priority};
}

void Nfa::add_literal(
    const std::string_view literal,
    const TokenType token_type,
    const int priority) {
    const StateId branch_start = add_state();
    add_epsilon(start_state_, branch_start);

    StateId current = branch_start;
    for (const unsigned char symbol : literal) {
        const StateId next = add_state();
        add_transition(current, symbol, next);
        current = next;
    }
    mark_accepting(current, token_type, priority);
}

void Nfa::add_identifier_pattern() {
    const StateId branch_start = add_state();
    const StateId accepting = add_state();
    add_epsilon(start_state_, branch_start);

    for (unsigned char symbol = 'a'; symbol <= 'z'; ++symbol) {
        add_transition(branch_start, symbol, accepting);
        add_transition(accepting, symbol, accepting);
    }
    for (unsigned char symbol = 'A'; symbol <= 'Z'; ++symbol) {
        add_transition(branch_start, symbol, accepting);
        add_transition(accepting, symbol, accepting);
    }
    for (unsigned char symbol = '0'; symbol <= '9'; ++symbol) {
        add_transition(accepting, symbol, accepting);
    }
    add_transition(branch_start, '_', accepting);
    add_transition(accepting, '_', accepting);
    mark_accepting(accepting, TokenType::Identifier, general_pattern_priority);
}

void Nfa::add_number_pattern() {
    const StateId branch_start = add_state();
    const StateId accepting = add_state();
    add_epsilon(start_state_, branch_start);

    for (unsigned char symbol = '0'; symbol <= '9'; ++symbol) {
        add_transition(branch_start, symbol, accepting);
        add_transition(accepting, symbol, accepting);
    }
    mark_accepting(accepting, TokenType::Number, general_pattern_priority);
}

void Nfa::add_whitespace_pattern() {
    const StateId branch_start = add_state();
    const StateId accepting = add_state();
    add_epsilon(start_state_, branch_start);

    for (const unsigned char symbol : std::string(" \t\r\n")) {
        add_transition(branch_start, symbol, accepting);
        add_transition(accepting, symbol, accepting);
    }
    mark_accepting(accepting, TokenType::Whitespace, whitespace_priority);
}

Dfa Dfa::determinize(const Nfa& nfa) {
    Dfa dfa;
    const auto initial = nfa.epsilon_closure({nfa.start_state()});

    std::map<std::set<StateId>, StateId> identifiers;
    std::queue<std::set<StateId>> pending;
    identifiers.emplace(initial, 0);
    pending.push(initial);

    DfaState initial_state{};
    initial_state.transitions.fill(invalid_state);
    initial_state.acceptance = nfa.acceptance_of(initial);
    initial_state.origins.assign(initial.begin(), initial.end());
    dfa.states_.push_back(std::move(initial_state));

    // Construcción por subconjuntos: cada estado determinista representa una
    // cerradura-epsilon de estados del AFN. Para cada símbolo se aplica
    // epsilon-closure(move(S, a)), lo que define una única transición.
    while (!pending.empty()) {
        const auto subset = pending.front();
        pending.pop();
        const StateId current_id = identifiers.at(subset);

        for (const unsigned char symbol : lexical_alphabet()) {
            const auto target = nfa.epsilon_closure(nfa.move(subset, symbol));
            if (target.empty()) {
                continue;
            }

            auto found = identifiers.find(target);
            if (found == identifiers.end()) {
                const StateId target_id = dfa.states_.size();
                identifiers.emplace(target, target_id);
                pending.push(target);

                DfaState target_state{};
                target_state.transitions.fill(invalid_state);
                target_state.acceptance = nfa.acceptance_of(target);
                target_state.origins.assign(target.begin(), target.end());
                dfa.states_.push_back(std::move(target_state));
                found = identifiers.find(target);
            }

            dfa.states_.at(current_id).transitions[symbol] = found->second;
        }
    }

    // El subconjunto vacío se representa con un sumidero explícito. Así delta
    // queda total sobre Sigma y la minimización puede razonar sobre un AFD
    // completo. El lexer detiene maximal munch antes de entrar al sumidero.
    dfa.sink_state_ = dfa.states_.size();
    DfaState sink{};
    sink.transitions.fill(invalid_state);
    for (const unsigned char symbol : lexical_alphabet()) {
        sink.transitions[symbol] = dfa.sink_state_;
    }
    dfa.states_.push_back(std::move(sink));

    for (auto& state : dfa.states_) {
        for (const unsigned char symbol : lexical_alphabet()) {
            if (state.transitions[symbol] == invalid_state) {
                state.transitions[symbol] = dfa.sink_state_;
            }
        }
    }

    dfa.start_state_ = 0;
    return dfa;
}

Dfa Dfa::minimize() const {
    if (states_.empty()) {
        return {};
    }

    // La partición inicial separa cada salida observable. Dos estados finales
    // que clasifican tokens distintos jamás comparten bloque, aun si aceptan
    // lenguajes residuales similares.
    std::map<int, StateId> acceptance_groups;
    std::vector<StateId> groups(states_.size(), 0);
    for (StateId state = 0; state < states_.size(); ++state) {
        const int key = states_[state].acceptance.has_value()
            ? static_cast<int>(states_[state].acceptance->token_type)
            : -1;
        const auto inserted = acceptance_groups.emplace(
            key,
            acceptance_groups.size());
        groups[state] = inserted.first->second;
    }

    // Refinamiento de Moore: la firma de un estado contiene su bloque actual y
    // el bloque alcanzado por cada símbolo. Se itera hasta el punto fijo.
    while (true) {
        std::map<std::vector<StateId>, StateId> signature_groups;
        std::vector<StateId> refined(states_.size(), 0);

        for (StateId state = 0; state < states_.size(); ++state) {
            std::vector<StateId> signature;
            signature.reserve(1 + lexical_alphabet().size());
            signature.push_back(groups[state]);
            for (const unsigned char symbol : lexical_alphabet()) {
                signature.push_back(groups[states_[state].transitions[symbol]]);
            }

            const auto inserted = signature_groups.emplace(
                std::move(signature),
                signature_groups.size());
            refined[state] = inserted.first->second;
        }

        if (refined == groups) {
            break;
        }
        groups = std::move(refined);
    }

    const StateId group_count =
        *std::max_element(groups.begin(), groups.end()) + 1;
    std::vector<std::vector<StateId>> members(group_count);
    for (StateId state = 0; state < states_.size(); ++state) {
        members[groups[state]].push_back(state);
    }

    std::vector<std::array<StateId, 128>> group_transitions(group_count);
    for (StateId group = 0; group < group_count; ++group) {
        group_transitions[group].fill(invalid_state);
        const StateId representative = members[group].front();
        for (const unsigned char symbol : lexical_alphabet()) {
            group_transitions[group][symbol] =
                groups[states_[representative].transitions[symbol]];
        }
    }

    // Se renumeran los bloques en BFS desde el inicio. Esto mantiene todos los
    // estados alcanzables y produce identificadores estables M0, M1, ... para
    // la matriz documentada.
    const StateId start_group = groups[start_state_];
    std::vector<StateId> group_to_new(group_count, invalid_state);
    std::vector<StateId> order;
    std::queue<StateId> pending;
    group_to_new[start_group] = 0;
    order.push_back(start_group);
    pending.push(start_group);

    while (!pending.empty()) {
        const StateId group = pending.front();
        pending.pop();
        for (const unsigned char symbol : lexical_alphabet()) {
            const StateId target = group_transitions[group][symbol];
            if (group_to_new[target] == invalid_state) {
                group_to_new[target] = order.size();
                order.push_back(target);
                pending.push(target);
            }
        }
    }

    Dfa minimized;
    minimized.start_state_ = 0;
    minimized.states_.reserve(order.size());
    for (const StateId group : order) {
        DfaState state{};
        state.transitions.fill(invalid_state);
        const StateId representative = members[group].front();
        state.acceptance = states_[representative].acceptance;
        state.origins = members[group];
        minimized.states_.push_back(std::move(state));
    }

    for (StateId new_state = 0; new_state < order.size(); ++new_state) {
        const StateId group = order[new_state];
        for (const unsigned char symbol : lexical_alphabet()) {
            minimized.states_[new_state].transitions[symbol] =
                group_to_new[group_transitions[group][symbol]];
        }
    }

    minimized.sink_state_ = group_to_new[groups[sink_state_]];
    return minimized;
}

StateId Dfa::start_state() const noexcept {
    return start_state_;
}

StateId Dfa::sink_state() const noexcept {
    return sink_state_;
}

std::size_t Dfa::state_count() const noexcept {
    return states_.size();
}

const std::vector<DfaState>& Dfa::states() const noexcept {
    return states_;
}

std::optional<StateId> Dfa::transition(
    const StateId state,
    const unsigned char symbol) const noexcept {
    if (state >= states_.size() || symbol >= 128 ||
        !is_lexical_alphabet_character(symbol)) {
        return std::nullopt;
    }
    const StateId target = states_[state].transitions[symbol];
    if (target == invalid_state || target >= states_.size()) {
        return std::nullopt;
    }
    return target;
}

std::optional<TokenType> Dfa::recognize(const std::string_view lexeme) const {
    if (states_.empty()) {
        return std::nullopt;
    }

    StateId state = start_state_;
    for (const unsigned char symbol : lexeme) {
        const auto target = transition(state, symbol);
        if (!target.has_value()) {
            return std::nullopt;
        }
        state = target.value();
    }
    return observable_acceptance(states_[state]);
}

bool Dfa::is_complete_and_deterministic() const noexcept {
    if (states_.empty() || start_state_ >= states_.size() ||
        sink_state_ >= states_.size()) {
        return false;
    }
    for (const auto& state : states_) {
        for (const unsigned char symbol : lexical_alphabet()) {
            if (state.transitions[symbol] >= states_.size()) {
                return false;
            }
        }
    }
    return true;
}

bool Dfa::all_states_reachable() const {
    if (states_.empty()) {
        return true;
    }

    std::vector<bool> reached(states_.size(), false);
    std::queue<StateId> pending;
    reached[start_state_] = true;
    pending.push(start_state_);

    while (!pending.empty()) {
        const StateId state = pending.front();
        pending.pop();
        for (const unsigned char symbol : lexical_alphabet()) {
            const StateId target = states_[state].transitions[symbol];
            if (!reached[target]) {
                reached[target] = true;
                pending.push(target);
            }
        }
    }

    return std::all_of(reached.begin(), reached.end(), [](const bool value) {
        return value;
    });
}

bool Dfa::equivalent_to(const Dfa& other) const {
    if (!is_complete_and_deterministic() ||
        !other.is_complete_and_deterministic()) {
        return false;
    }

    using StatePair = std::pair<StateId, StateId>;
    std::set<StatePair> visited;
    std::queue<StatePair> pending;
    pending.push({start_state_, other.start_state_});

    while (!pending.empty()) {
        const auto pair = pending.front();
        pending.pop();
        if (!visited.insert(pair).second) {
            continue;
        }

        if (!same_observable_acceptance(
                states_[pair.first],
                other.states_[pair.second])) {
            return false;
        }

        for (const unsigned char symbol : lexical_alphabet()) {
            pending.push({
                states_[pair.first].transitions[symbol],
                other.states_[pair.second].transitions[symbol]
            });
        }
    }

    return true;
}

bool Dfa::has_equivalent_states() const {
    const std::size_t count = states_.size();
    std::vector<std::vector<bool>> distinguishable(
        count,
        std::vector<bool>(count, false));

    for (StateId left = 0; left < count; ++left) {
        for (StateId right = left + 1; right < count; ++right) {
            distinguishable[left][right] =
                !same_observable_acceptance(states_[left], states_[right]);
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (StateId left = 0; left < count; ++left) {
            for (StateId right = left + 1; right < count; ++right) {
                if (distinguishable[left][right]) {
                    continue;
                }
                for (const unsigned char symbol : lexical_alphabet()) {
                    StateId target_left = states_[left].transitions[symbol];
                    StateId target_right = states_[right].transitions[symbol];
                    if (target_left == target_right) {
                        continue;
                    }
                    if (target_left > target_right) {
                        std::swap(target_left, target_right);
                    }
                    if (distinguishable[target_left][target_right]) {
                        distinguishable[left][right] = true;
                        changed = true;
                        break;
                    }
                }
            }
        }
    }

    for (StateId left = 0; left < count; ++left) {
        for (StateId right = left + 1; right < count; ++right) {
            if (!distinguishable[left][right]) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::vector<unsigned char>> Dfa::symbol_classes() const {
    std::map<std::vector<StateId>, std::size_t> class_by_signature;
    std::vector<std::vector<unsigned char>> classes;

    for (const unsigned char symbol : lexical_alphabet()) {
        std::vector<StateId> signature;
        signature.reserve(states_.size());
        for (const auto& state : states_) {
            signature.push_back(state.transitions[symbol]);
        }

        const auto found = class_by_signature.find(signature);
        if (found == class_by_signature.end()) {
            const std::size_t identifier = classes.size();
            class_by_signature.emplace(std::move(signature), identifier);
            classes.push_back({symbol});
        } else {
            classes[found->second].push_back(symbol);
        }
    }

    return classes;
}

std::vector<std::vector<std::vector<StateId>>>
Dfa::minimization_partition_trace() const {
    std::vector<std::vector<std::vector<StateId>>> trace;
    if (states_.empty()) {
        return trace;
    }

    std::map<int, StateId> acceptance_groups;
    std::vector<StateId> groups(states_.size(), 0);
    for (StateId state = 0; state < states_.size(); ++state) {
        const int key = states_[state].acceptance.has_value()
            ? static_cast<int>(states_[state].acceptance->token_type)
            : -1;
        const auto inserted = acceptance_groups.emplace(
            key,
            acceptance_groups.size());
        groups[state] = inserted.first->second;
    }

    const auto append_partition = [&trace](const std::vector<StateId>& assignments) {
        const StateId count =
            *std::max_element(assignments.begin(), assignments.end()) + 1;
        std::vector<std::vector<StateId>> partition(count);
        for (StateId state = 0; state < assignments.size(); ++state) {
            partition[assignments[state]].push_back(state);
        }
        trace.push_back(std::move(partition));
    };

    append_partition(groups);
    while (true) {
        std::map<std::vector<StateId>, StateId> signature_groups;
        std::vector<StateId> refined(states_.size(), 0);
        for (StateId state = 0; state < states_.size(); ++state) {
            std::vector<StateId> signature;
            signature.reserve(1 + lexical_alphabet().size());
            signature.push_back(groups[state]);
            for (const unsigned char symbol : lexical_alphabet()) {
                signature.push_back(groups[states_[state].transitions[symbol]]);
            }
            const auto inserted = signature_groups.emplace(
                std::move(signature),
                signature_groups.size());
            refined[state] = inserted.first->second;
        }

        if (refined == groups) {
            break;
        }
        groups = std::move(refined);
        append_partition(groups);
    }

    return trace;
}

LexerAutomata::LexerAutomata()
    : nfa_(Nfa::build_lexer_nfa()),
      determinized_dfa_(Dfa::determinize(nfa_)),
      minimized_dfa_(determinized_dfa_.minimize()) {}

const Nfa& LexerAutomata::nfa() const noexcept {
    return nfa_;
}

const Dfa& LexerAutomata::determinized_dfa() const noexcept {
    return determinized_dfa_;
}

const Dfa& LexerAutomata::minimized_dfa() const noexcept {
    return minimized_dfa_;
}

} // namespace lcero::lexer
