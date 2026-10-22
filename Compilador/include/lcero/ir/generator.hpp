#pragma once

#include "lcero/ast/ast.hpp"
#include "lcero/common/diagnostic.hpp"
#include "lcero/ir/ir.hpp"
#include "lcero/semantic/symbol_table.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace lcero::ir {

struct GenerationResult {
    bool succeeded = false;
    Program program;
};

class Generator {
public:
    [[nodiscard]] GenerationResult generate(
        const ast::Program& ast_program,
        const semantic::SymbolTable& symbols,
        const std::filesystem::path& source_path,
        common::DiagnosticBag& diagnostics);

private:
    [[nodiscard]] VirtualTemporary make_temporary() noexcept;
    [[nodiscard]] LabelId make_label() noexcept;
    void emit(Instruction instruction, ast::SourcePosition position);

    [[nodiscard]] bool translate_statements(
        const std::vector<std::unique_ptr<ast::Statement>>& statements);
    [[nodiscard]] bool translate_statement(const ast::Statement& statement);
    [[nodiscard]] bool translate_block(const ast::Block& block);
    [[nodiscard]] std::optional<VirtualTemporary> translate_expression(
        const ast::Expression& expression);
    [[nodiscard]] std::optional<VirtualTemporary> translate_condition(
        const ast::Condition& condition);

    [[nodiscard]] std::optional<VariableId> variable_id(
        const std::string& name,
        const ast::SourcePosition& position);
    void report_internal(
        const std::string& message,
        const ast::SourcePosition& position);
    [[nodiscard]] static std::string canonical_decimal(std::string value);

    Program program_;
    std::map<std::string, VariableId> variables_by_name_;
    const semantic::SymbolTable* symbols_ = nullptr;
    std::filesystem::path source_path_;
    common::DiagnosticBag* diagnostics_ = nullptr;
    bool error_reported_ = false;
};

} // namespace lcero::ir
