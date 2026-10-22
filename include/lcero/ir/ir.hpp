#pragma once

#include "lcero/ast/ast.hpp"

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace lcero::ir {

struct VirtualTemporary {
    std::size_t id = 0;
};

struct VariableId {
    std::size_t id = 0;
};

struct LabelId {
    std::size_t id = 0;
};

struct Variable {
    VariableId id;
    std::string name;
    std::size_t address = 0;
    ast::SourcePosition declaration_position;
};

enum class Opcode {
    Constant,
    LoadVariable,
    StoreVariable,
    Add,
    Subtract,
    Multiply,
    Divide,
    CompareEqual,
    CompareNotEqual,
    CompareLess,
    CompareLessEqual,
    CompareGreater,
    CompareGreaterEqual,
    Jump,
    JumpIfZero,
    Label,
    Print,
    Halt
};

struct ConstantInstruction {
    VirtualTemporary destination;
    std::string decimal_value;
};

struct LoadVariableInstruction {
    VirtualTemporary destination;
    VariableId variable;
};

struct StoreVariableInstruction {
    VariableId variable;
    VirtualTemporary source;
};

struct BinaryInstruction {
    Opcode operation;
    VirtualTemporary destination;
    VirtualTemporary left;
    VirtualTemporary right;
};

struct ComparisonInstruction {
    Opcode operation;
    VirtualTemporary destination;
    VirtualTemporary left;
    VirtualTemporary right;
};

struct JumpInstruction {
    LabelId target;
};

struct JumpIfZeroInstruction {
    VirtualTemporary condition;
    LabelId target;
};

struct LabelInstruction {
    LabelId label;
};

struct PrintInstruction {
    VirtualTemporary source;
};

struct HaltInstruction {};

using Instruction = std::variant<
    ConstantInstruction,
    LoadVariableInstruction,
    StoreVariableInstruction,
    BinaryInstruction,
    ComparisonInstruction,
    JumpInstruction,
    JumpIfZeroInstruction,
    LabelInstruction,
    PrintInstruction,
    HaltInstruction>;

struct InstructionRecord {
    Instruction instruction;
    ast::SourcePosition position;
};

struct Program {
    std::vector<Variable> variables;
    std::vector<InstructionRecord> instructions;
    std::size_t temporary_count = 0;
    std::size_t label_count = 0;
};

[[nodiscard]] Opcode opcode_of(const Instruction& instruction);
[[nodiscard]] const char* opcode_name(Opcode opcode) noexcept;
[[nodiscard]] bool is_arithmetic_opcode(Opcode opcode) noexcept;
[[nodiscard]] bool is_comparison_opcode(Opcode opcode) noexcept;

} // namespace lcero::ir
