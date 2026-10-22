#include "lcero/ir/generator.hpp"

#include <utility>

namespace lcero::ir {

GenerationResult Generator::generate(
    const ast::Program& ast_program,
    const semantic::SymbolTable& symbols,
    const std::filesystem::path& source_path,
    common::DiagnosticBag& diagnostics) {
    program_ = Program{};
    variables_by_name_.clear();
    symbols_ = &symbols;
    source_path_ = source_path;
    diagnostics_ = &diagnostics;
    error_reported_ = false;

    // Las direcciones siguen el orden de declaraciones del AST, no el orden
    // alfabetico de la tabla. Asi la funcion nombre -> slot es determinista.
    for (const auto& declaration : ast_program.declarations) {
        if (!symbols.contains(declaration.name)) {
            report_internal(
                "La tabla semantica no contiene la declaracion '" +
                    declaration.name + "'.",
                declaration.position);
            return {};
        }
        const VariableId id{program_.variables.size()};
        if (!variables_by_name_.emplace(declaration.name, id).second) {
            report_internal(
                "La RI recibio una declaracion duplicada: '" +
                    declaration.name + "'.",
                declaration.position);
            return {};
        }
        program_.variables.push_back(Variable{
            id,
            declaration.name,
            id.id,
            declaration.position
        });
    }

    if (program_.variables.size() != symbols.size()) {
        report_internal(
            "El AST y la tabla de simbolos difieren en cantidad de variables.",
            ast_program.position);
        return {};
    }

    if (!translate_statements(ast_program.statements)) {
        return GenerationResult{false, std::move(program_)};
    }
    emit(HaltInstruction{}, ast_program.position);
    return GenerationResult{true, std::move(program_)};
}

VirtualTemporary Generator::make_temporary() noexcept {
    return VirtualTemporary{program_.temporary_count++};
}

LabelId Generator::make_label() noexcept {
    return LabelId{program_.label_count++};
}

void Generator::emit(
    Instruction instruction,
    const ast::SourcePosition position) {
    program_.instructions.push_back(
        InstructionRecord{std::move(instruction), position});
}

bool Generator::translate_statements(
    const std::vector<std::unique_ptr<ast::Statement>>& statements) {
    for (const auto& statement : statements) {
        if (!translate_statement(*statement)) {
            return false;
        }
    }
    return true;
}

bool Generator::translate_statement(const ast::Statement& statement) {
    if (const auto* assignment =
            dynamic_cast<const ast::AssignmentStatement*>(&statement)) {
        const auto value = translate_expression(*assignment->expression);
        const auto variable = variable_id(assignment->name, assignment->position);
        if (!value.has_value() || !variable.has_value()) {
            return false;
        }
        emit(StoreVariableInstruction{variable.value(), value.value()},
             assignment->position);
        return true;
    }

    if (const auto* print = dynamic_cast<const ast::PrintStatement*>(&statement)) {
        const auto value = translate_expression(*print->expression);
        if (!value.has_value()) {
            return false;
        }
        emit(PrintInstruction{value.value()}, print->position);
        return true;
    }

    if (const auto* branch = dynamic_cast<const ast::IfStatement*>(&statement)) {
        const auto condition = translate_condition(*branch->condition);
        if (!condition.has_value()) {
            return false;
        }

        if (branch->else_block == nullptr) {
            const auto end_label = make_label();
            emit(JumpIfZeroInstruction{condition.value(), end_label}, branch->position);
            if (!translate_block(*branch->then_block)) {
                return false;
            }
            emit(LabelInstruction{end_label}, branch->position);
            return true;
        }

        const auto else_label = make_label();
        const auto end_label = make_label();
        emit(JumpIfZeroInstruction{condition.value(), else_label}, branch->position);
        if (!translate_block(*branch->then_block)) {
            return false;
        }
        emit(JumpInstruction{end_label}, branch->position);
        emit(LabelInstruction{else_label}, branch->else_block->position);
        if (!translate_block(*branch->else_block)) {
            return false;
        }
        emit(LabelInstruction{end_label}, branch->position);
        return true;
    }

    if (const auto* loop = dynamic_cast<const ast::WhileStatement*>(&statement)) {
        const auto start_label = make_label();
        const auto end_label = make_label();
        emit(LabelInstruction{start_label}, loop->position);

        // La condicion se emite despues de LABEL y antes del cuerpo, de modo
        // que el salto de retorno vuelve a calcularla en cada iteracion.
        const auto condition = translate_condition(*loop->condition);
        if (!condition.has_value()) {
            return false;
        }
        emit(JumpIfZeroInstruction{condition.value(), end_label}, loop->position);
        if (!translate_block(*loop->body)) {
            return false;
        }
        emit(JumpInstruction{start_label}, loop->position);
        emit(LabelInstruction{end_label}, loop->position);
        return true;
    }

    report_internal("Tipo de instruccion AST desconocido durante generacion.",
                    statement.position);
    return false;
}

bool Generator::translate_block(const ast::Block& block) {
    return translate_statements(block.statements);
}

std::optional<VirtualTemporary> Generator::translate_expression(
    const ast::Expression& expression) {
    if (const auto* literal = dynamic_cast<const ast::IntegerLiteral*>(&expression)) {
        const auto destination = make_temporary();
        emit(ConstantInstruction{
            destination,
            canonical_decimal(literal->value)
        }, literal->position);
        return destination;
    }

    if (const auto* variable =
            dynamic_cast<const ast::VariableExpression*>(&expression)) {
        const auto id = variable_id(variable->name, variable->position);
        if (!id.has_value()) {
            return std::nullopt;
        }
        const auto destination = make_temporary();
        emit(LoadVariableInstruction{destination, id.value()}, variable->position);
        return destination;
    }

    if (const auto* unary = dynamic_cast<const ast::UnaryExpression*>(&expression)) {
        // LCERO_IR 1 no posee NEG:  -e se traduce exactamente como 0 - e.
        const auto zero = make_temporary();
        emit(ConstantInstruction{zero, "0"}, unary->position);
        const auto operand = translate_expression(*unary->operand);
        if (!operand.has_value()) {
            return std::nullopt;
        }
        const auto destination = make_temporary();
        emit(BinaryInstruction{
            Opcode::Subtract,
            destination,
            zero,
            operand.value()
        }, unary->position);
        return destination;
    }

    if (const auto* binary = dynamic_cast<const ast::BinaryExpression*>(&expression)) {
        // El recorrido izquierda-derecha sigue la estructura ya resuelta del
        // AST; no se vuelven a inferir precedencia ni asociatividad desde texto.
        const auto left = translate_expression(*binary->left);
        if (!left.has_value()) {
            return std::nullopt;
        }
        const auto right = translate_expression(*binary->right);
        if (!right.has_value()) {
            return std::nullopt;
        }

        Opcode operation = Opcode::Add;
        switch (binary->operation) {
        case ast::BinaryOperator::Add: operation = Opcode::Add; break;
        case ast::BinaryOperator::Subtract: operation = Opcode::Subtract; break;
        case ast::BinaryOperator::Multiply: operation = Opcode::Multiply; break;
        case ast::BinaryOperator::Divide: operation = Opcode::Divide; break;
        }

        const auto destination = make_temporary();
        emit(BinaryInstruction{
            operation,
            destination,
            left.value(),
            right.value()
        }, binary->position);
        return destination;
    }

    report_internal("Tipo de expresion AST desconocido durante generacion.",
                    expression.position);
    return std::nullopt;
}

std::optional<VirtualTemporary> Generator::translate_condition(
    const ast::Condition& condition) {
    const auto left = translate_expression(*condition.left);
    if (!left.has_value()) {
        return std::nullopt;
    }
    const auto right = translate_expression(*condition.right);
    if (!right.has_value()) {
        return std::nullopt;
    }

    Opcode operation = Opcode::CompareEqual;
    switch (condition.operation) {
    case ast::ComparisonOperator::Equal: operation = Opcode::CompareEqual; break;
    case ast::ComparisonOperator::NotEqual: operation = Opcode::CompareNotEqual; break;
    case ast::ComparisonOperator::Less: operation = Opcode::CompareLess; break;
    case ast::ComparisonOperator::Greater: operation = Opcode::CompareGreater; break;
    case ast::ComparisonOperator::LessEqual: operation = Opcode::CompareLessEqual; break;
    case ast::ComparisonOperator::GreaterEqual: operation = Opcode::CompareGreaterEqual; break;
    }

    const auto destination = make_temporary();
    emit(ComparisonInstruction{
        operation,
        destination,
        left.value(),
        right.value()
    }, condition.position);
    return destination;
}

std::optional<VariableId> Generator::variable_id(
    const std::string& name,
    const ast::SourcePosition& position) {
    const auto item = variables_by_name_.find(name);
    if (item == variables_by_name_.end() || !symbols_->contains(name)) {
        report_internal(
            "La RI encontro una variable sin simbolo validado: '" + name + "'.",
            position);
        return std::nullopt;
    }
    return item->second;
}

void Generator::report_internal(
    const std::string& message,
    const ast::SourcePosition& position) {
    if (error_reported_) {
        return;
    }
    error_reported_ = true;
    diagnostics_->report(
        common::DiagnosticCategory::CodeGeneration,
        common::DiagnosticSeverity::Error,
        message,
        common::SourceLocation{source_path_, position.line, position.column});
}

std::string Generator::canonical_decimal(std::string value) {
    const auto first_nonzero = value.find_first_not_of('0');
    if (first_nonzero == std::string::npos) {
        return "0";
    }
    value.erase(0, first_nonzero);
    return value;
}

} // namespace lcero::ir
