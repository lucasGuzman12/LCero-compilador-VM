#include "lcero/ir/ir.hpp"

#include <type_traits>

namespace lcero::ir {

Opcode opcode_of(const Instruction& instruction) {
    return std::visit([](const auto& value) -> Opcode {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ConstantInstruction>) {
            return Opcode::Constant;
        } else if constexpr (std::is_same_v<T, LoadVariableInstruction>) {
            return Opcode::LoadVariable;
        } else if constexpr (std::is_same_v<T, StoreVariableInstruction>) {
            return Opcode::StoreVariable;
        } else if constexpr (std::is_same_v<T, BinaryInstruction> ||
                             std::is_same_v<T, ComparisonInstruction>) {
            return value.operation;
        } else if constexpr (std::is_same_v<T, JumpInstruction>) {
            return Opcode::Jump;
        } else if constexpr (std::is_same_v<T, JumpIfZeroInstruction>) {
            return Opcode::JumpIfZero;
        } else if constexpr (std::is_same_v<T, LabelInstruction>) {
            return Opcode::Label;
        } else if constexpr (std::is_same_v<T, PrintInstruction>) {
            return Opcode::Print;
        } else {
            return Opcode::Halt;
        }
    }, instruction);
}

const char* opcode_name(const Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::Constant: return "CONST";
    case Opcode::LoadVariable: return "LOAD_VAR";
    case Opcode::StoreVariable: return "STORE_VAR";
    case Opcode::Add: return "ADD";
    case Opcode::Subtract: return "SUB";
    case Opcode::Multiply: return "MUL";
    case Opcode::Divide: return "DIV";
    case Opcode::CompareEqual: return "CMP_EQ";
    case Opcode::CompareNotEqual: return "CMP_NE";
    case Opcode::CompareLess: return "CMP_LT";
    case Opcode::CompareLessEqual: return "CMP_LE";
    case Opcode::CompareGreater: return "CMP_GT";
    case Opcode::CompareGreaterEqual: return "CMP_GE";
    case Opcode::Jump: return "JMP";
    case Opcode::JumpIfZero: return "JZ";
    case Opcode::Label: return "LABEL";
    case Opcode::Print: return "PRINT";
    case Opcode::Halt: return "HALT";
    }
    return "UNKNOWN";
}

bool is_arithmetic_opcode(const Opcode opcode) noexcept {
    return opcode == Opcode::Add || opcode == Opcode::Subtract ||
        opcode == Opcode::Multiply || opcode == Opcode::Divide;
}

bool is_comparison_opcode(const Opcode opcode) noexcept {
    return opcode == Opcode::CompareEqual || opcode == Opcode::CompareNotEqual ||
        opcode == Opcode::CompareLess || opcode == Opcode::CompareLessEqual ||
        opcode == Opcode::CompareGreater || opcode == Opcode::CompareGreaterEqual;
}

} // namespace lcero::ir
