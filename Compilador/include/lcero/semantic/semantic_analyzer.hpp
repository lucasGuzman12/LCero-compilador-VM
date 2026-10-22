#pragma once

#include "lcero/ast/ast.hpp"
#include "lcero/common/diagnostic.hpp"
#include "lcero/semantic/symbol_table.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace lcero::semantic {

using InitializationSet = std::set<std::string>;

enum class TransferKind {
    Assignment,
    Print,
    IfWithElse,
    IfWithoutElse,
    While
};

// Registro observable de D;I |- S => I'. Los estados de rama permiten auditar
// la interseccion de if/else sin acoplar las pruebas al recorrido interno.
struct TransferRecord {
    TransferKind kind;
    ast::SourcePosition position;
    InitializationSet input;
    InitializationSet output;
    InitializationSet then_output;
    InitializationSet else_output;
};

struct SemanticAnalysisResult {
    bool succeeded = false;
    SymbolTable symbol_table;
    InitializationSet definitely_initialized;
    std::vector<TransferRecord> transfers;
};

class SemanticAnalyzer {
public:
    [[nodiscard]] SemanticAnalysisResult analyze(
        const ast::Program& program,
        const std::filesystem::path& source_path,
        common::DiagnosticBag& diagnostics);

private:
    [[nodiscard]] bool declare_all(const std::vector<ast::Declaration>& declarations);
    [[nodiscard]] bool analyze_statements(
        const std::vector<std::unique_ptr<ast::Statement>>& statements,
        InitializationSet& initialized);
    [[nodiscard]] bool analyze_statement(
        const ast::Statement& statement,
        InitializationSet& initialized);
    [[nodiscard]] bool analyze_block(
        const ast::Block& block,
        InitializationSet& initialized);
    [[nodiscard]] bool validate_condition(
        const ast::Condition& condition,
        const InitializationSet& initialized);
    [[nodiscard]] bool validate_expression(
        const ast::Expression& expression,
        const InitializationSet& initialized);
    [[nodiscard]] bool validate_invariant(
        const InitializationSet& initialized,
        const ast::SourcePosition& position);

    void report_duplicate(
        const ast::Declaration& declaration,
        const Symbol& previous);
    void report_undeclared(
        const std::string& name,
        const ast::SourcePosition& position);
    void report_uninitialized(
        const std::string& name,
        const ast::SourcePosition& position);
    void report_internal(
        const std::string& message,
        const ast::SourcePosition& position);

    [[nodiscard]] common::SourceLocation location(
        const ast::SourcePosition& position) const;

    SymbolTable symbol_table_;
    std::vector<TransferRecord> transfers_;
    std::filesystem::path source_path_;
    common::DiagnosticBag* diagnostics_ = nullptr;
    bool error_reported_ = false;
};

[[nodiscard]] const char* transfer_kind_name(TransferKind kind) noexcept;

} // namespace lcero::semantic
