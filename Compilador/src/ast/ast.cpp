#include "lcero/ast/ast.hpp"

#include <utility>

namespace lcero::ast {

Node::Node(const SourcePosition source_position)
    : position(source_position) {}

Declaration::Declaration(
    const SourcePosition source_position,
    std::string variable_name)
    : Node(source_position), name(std::move(variable_name)) {}

IntegerLiteral::IntegerLiteral(
    const SourcePosition source_position,
    std::string decimal_text)
    : Expression(source_position), value(std::move(decimal_text)) {}

VariableExpression::VariableExpression(
    const SourcePosition source_position,
    std::string variable_name)
    : Expression(source_position), name(std::move(variable_name)) {}

UnaryExpression::UnaryExpression(
    const SourcePosition source_position,
    const UnaryOperator operation,
    std::unique_ptr<Expression> child)
    : Expression(source_position),
      operation(operation),
      operand(std::move(child)) {}

BinaryExpression::BinaryExpression(
    const SourcePosition source_position,
    const BinaryOperator operation,
    std::unique_ptr<Expression> left_child,
    std::unique_ptr<Expression> right_child)
    : Expression(source_position),
      operation(operation),
      left(std::move(left_child)),
      right(std::move(right_child)) {}

Condition::Condition(
    const SourcePosition source_position,
    const ComparisonOperator operation,
    std::unique_ptr<Expression> left_expression,
    std::unique_ptr<Expression> right_expression)
    : Node(source_position),
      operation(operation),
      left(std::move(left_expression)),
      right(std::move(right_expression)) {}

Block::Block(
    const SourcePosition source_position,
    std::vector<std::unique_ptr<Statement>> block_statements)
    : Node(source_position), statements(std::move(block_statements)) {}

AssignmentStatement::AssignmentStatement(
    const SourcePosition source_position,
    std::string variable_name,
    std::unique_ptr<Expression> assigned_expression)
    : Statement(source_position),
      name(std::move(variable_name)),
      expression(std::move(assigned_expression)) {}

PrintStatement::PrintStatement(
    const SourcePosition source_position,
    std::unique_ptr<Expression> printed_expression)
    : Statement(source_position), expression(std::move(printed_expression)) {}

IfStatement::IfStatement(
    const SourcePosition source_position,
    std::unique_ptr<Condition> branch_condition,
    std::unique_ptr<Block> then_branch,
    std::unique_ptr<Block> optional_else_branch)
    : Statement(source_position),
      condition(std::move(branch_condition)),
      then_block(std::move(then_branch)),
      else_block(std::move(optional_else_branch)) {}

WhileStatement::WhileStatement(
    const SourcePosition source_position,
    std::unique_ptr<Condition> loop_condition,
    std::unique_ptr<Block> loop_body)
    : Statement(source_position),
      condition(std::move(loop_condition)),
      body(std::move(loop_body)) {}

Program::Program(
    const SourcePosition source_position,
    std::vector<Declaration> program_declarations,
    std::vector<std::unique_ptr<Statement>> program_statements)
    : Node(source_position),
      declarations(std::move(program_declarations)),
      statements(std::move(program_statements)) {}

} // namespace lcero::ast
