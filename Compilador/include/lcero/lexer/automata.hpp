#pragma once

#include "lcero/lexer/token.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace lcero::lexer {

using StateId = std::size_t;

struct Acceptance {
    TokenType token_type;
    int priority;
};

[[nodiscard]] bool operator==(const Acceptance& left, const Acceptance& right) noexcept;
[[nodiscard]] bool operator!=(const Acceptance& left, const Acceptance& right) noexcept;

struct NfaState {
    std::array<std::vector<StateId>, 128> transitions;
    std::vector<StateId> epsilon_transitions;
    std::optional<Acceptance> acceptance;
};

class Nfa {
public:
    Nfa();

    [[nodiscard]] static Nfa build_lexer_nfa();

    [[nodiscard]] StateId start_state() const noexcept;
    [[nodiscard]] std::size_t state_count() const noexcept;
    [[nodiscard]] const std::vector<NfaState>& states() const noexcept;

    [[nodiscard]] std::set<StateId> epsilon_closure(
        const std::set<StateId>& input_states) const;
    [[nodiscard]] std::set<StateId> move(
        const std::set<StateId>& input_states,
        unsigned char symbol) const;
    [[nodiscard]] std::optional<Acceptance> acceptance_of(
        const std::set<StateId>& input_states) const;
    [[nodiscard]] std::optional<TokenType> recognize(std::string_view lexeme) const;

private:
    StateId add_state();
    void add_epsilon(StateId from, StateId to);
    void add_transition(StateId from, unsigned char symbol, StateId to);
    void mark_accepting(StateId state, TokenType token_type, int priority);
    void add_literal(std::string_view literal, TokenType token_type, int priority);
    void add_identifier_pattern();
    void add_number_pattern();
    void add_whitespace_pattern();

    std::vector<NfaState> states_;
    StateId start_state_ = 0;
};

struct DfaState {
    std::array<StateId, 128> transitions;
    std::optional<Acceptance> acceptance;
    std::vector<StateId> origins;
};

class Dfa {
public:
    static constexpr StateId invalid_state = std::numeric_limits<StateId>::max();

    Dfa() = default;

    [[nodiscard]] static Dfa determinize(const Nfa& nfa);
    [[nodiscard]] Dfa minimize() const;

    [[nodiscard]] StateId start_state() const noexcept;
    [[nodiscard]] StateId sink_state() const noexcept;
    [[nodiscard]] std::size_t state_count() const noexcept;
    [[nodiscard]] const std::vector<DfaState>& states() const noexcept;
    [[nodiscard]] std::optional<StateId> transition(
        StateId state,
        unsigned char symbol) const noexcept;
    [[nodiscard]] std::optional<TokenType> recognize(std::string_view lexeme) const;

    [[nodiscard]] bool is_complete_and_deterministic() const noexcept;
    [[nodiscard]] bool all_states_reachable() const;
    [[nodiscard]] bool equivalent_to(const Dfa& other) const;
    [[nodiscard]] bool has_equivalent_states() const;
    [[nodiscard]] std::vector<std::vector<unsigned char>> symbol_classes() const;
    [[nodiscard]] std::vector<std::vector<std::vector<StateId>>>
        minimization_partition_trace() const;

private:
    std::vector<DfaState> states_;
    StateId start_state_ = 0;
    StateId sink_state_ = invalid_state;
};

class LexerAutomata {
public:
    LexerAutomata();

    [[nodiscard]] const Nfa& nfa() const noexcept;
    [[nodiscard]] const Dfa& determinized_dfa() const noexcept;
    [[nodiscard]] const Dfa& minimized_dfa() const noexcept;

private:
    Nfa nfa_;
    Dfa determinized_dfa_;
    Dfa minimized_dfa_;
};

[[nodiscard]] const std::vector<unsigned char>& lexical_alphabet();
[[nodiscard]] bool is_lexical_alphabet_character(unsigned char symbol) noexcept;
[[nodiscard]] std::string display_symbol(unsigned char symbol);

} // namespace lcero::lexer
