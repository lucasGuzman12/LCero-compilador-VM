#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace lcero::ast {

struct SourcePosition {
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t offset = 0;
};

struct Node {
    explicit Node(SourcePosition source_position);
    virtual ~Node() = default;

    SourcePosition position;
};

struct Expression : Node {
    using Node::Node;
    ~Expression() override = default;
};

struct Statement : Node {
    using Node::Node;
    ~Statement() override = default;
};

struct Declaration final : Node {
    Declaration(SourcePosition source_position, std::string variable_name);

    std::string name;
};

struct IntegerLiteral final : Expression {
    IntegerLiteral(SourcePosition source_position, std::string decimal_text);

    std::string value;
};

struct VariableExpression final : Expression {
    VariableExpression(SourcePosition source_position, std::string variable_name);

    std::string name;
};

enum class UnaryOperator {
    Negate
};

struct UnaryExpression final : Expression {
    UnaryExpression(
        SourcePosition source_position,
        UnaryOperator operation,
        std::unique_ptr<Expression> child);

    UnaryOperator operation;
    std::unique_ptr<Expression> operand;
};

enum class BinaryOperator {
    Add,
    Subtract,
    Multiply,
    Divide
};

struct BinaryExpression final : Expression {
    BinaryExpression(
        SourcePosition source_position,
        BinaryOperator operation,
        std::unique_ptr<Expression> left_child,
        std::unique_ptr<Expression> right_child);

    BinaryOperator operation;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;
};

enum class ComparisonOperator {
    Equal,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual
};

struct Condition final : Node {
    Condition(
        SourcePosition source_position,
        ComparisonOperator operation,
        std::unique_ptr<Expression> left_expression,
        std::unique_ptr<Expression> right_expression);

    ComparisonOperator operation;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;
};

struct Block final : Node {
    Block(
        SourcePosition source_position,
        std::vector<std::unique_ptr<Statement>> block_statements);

    std::vector<std::unique_ptr<Statement>> statements;
};

struct AssignmentStatement final : Statement {
    AssignmentStatement(
        SourcePosition source_position,
        std::string variable_name,
        std::unique_ptr<Expression> assigned_expression);

    std::string name;
    std::unique_ptr<Expression> expression;
};

struct PrintStatement final : Statement {
    PrintStatement(
        SourcePosition source_position,
        std::unique_ptr<Expression> printed_expression);

    std::unique_ptr<Expression> expression;
};

struct IfStatement final : Statement {
    IfStatement(
        SourcePosition source_position,
        std::unique_ptr<Condition> branch_condition,
        std::unique_ptr<Block> then_branch,
        std::unique_ptr<Block> optional_else_branch);

    std::unique_ptr<Condition> condition;
    std::unique_ptr<Block> then_block;
    std::unique_ptr<Block> else_block;
};

struct WhileStatement final : Statement {
    WhileStatement(
        SourcePosition source_position,
        std::unique_ptr<Condition> loop_condition,
        std::unique_ptr<Block> loop_body);

    std::unique_ptr<Condition> condition;
    std::unique_ptr<Block> body;
};

struct Program final : Node {
    Program(
        SourcePosition source_position,
        std::vector<Declaration> program_declarations,
        std::vector<std::unique_ptr<Statement>> program_statements);

    std::vector<Declaration> declarations;
    std::vector<std::unique_ptr<Statement>> statements;
};

} // namespace lcero::ast
