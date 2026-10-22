#include "lcero/vm/instruction.hpp"

#include <utility>

namespace lcero::vm {
namespace {

bool has_canonical_unsigned_syntax(const std::string_view text) noexcept {
    if (text == "0") {
        return true;
    }

    if (text.empty() || text.front() < '1' || text.front() > '9') {
        return false;
    }

    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
    }

    return true;
}

// La conversion acumula digitos directamente sobre cpp_int. No pasa por stoi,
// stoll, coma flotante ni ningun tipo de ancho fijo.
Integer parse_unsigned_digits(const std::string_view digits) {
    Integer value = 0;
    for (const char digit : digits) {
        value *= 10;
        value += static_cast<unsigned int>(digit - '0');
    }
    return value;
}

Diagnostic invalid_decimal(const DiagnosticCode code,
                           std::string message) {
    return {DiagnosticCategory::load, code, std::move(message)};
}

}  // namespace

Program::Program(std::vector<Instruction> instructions)
    : instructions_(std::move(instructions)) {}

std::size_t Program::size() const noexcept {
    return instructions_.size();
}

bool Program::empty() const noexcept {
    return instructions_.empty();
}

const Instruction& Program::at(const std::size_t index) const {
    return instructions_.at(index);
}

const std::vector<Instruction>& Program::instructions() const noexcept {
    return instructions_;
}

std::string_view opcode_name(const Opcode opcode) noexcept {
    switch (opcode) {
        case Opcode::constant:
            return "CONST";
        case Opcode::load:
            return "LOAD";
        case Opcode::store:
            return "STORE";
        case Opcode::move:
            return "MOV";
        case Opcode::add:
            return "ADD";
        case Opcode::subtract:
            return "SUB";
        case Opcode::multiply:
            return "MUL";
        case Opcode::divide:
            return "DIV";
        case Opcode::compare_equal:
            return "CMP_EQ";
        case Opcode::compare_not_equal:
            return "CMP_NE";
        case Opcode::compare_less:
            return "CMP_LT";
        case Opcode::compare_less_equal:
            return "CMP_LE";
        case Opcode::compare_greater:
            return "CMP_GT";
        case Opcode::compare_greater_equal:
            return "CMP_GE";
        case Opcode::jump:
            return "JMP";
        case Opcode::jump_if_zero:
            return "JZ";
        case Opcode::print:
            return "PRINT";
        case Opcode::halt:
            return "HALT";
    }

    return "OPCODE_INVALIDO";
}

Result<RegisterId> make_register_id(const std::uint32_t index) {
    if (index > 3U) {
        return Result<RegisterId>::failure(
            {DiagnosticCategory::load, DiagnosticCode::invalid_register,
             "LCERO_IR 1 solo define los registros R0..R3"});
    }

    return Result<RegisterId>::success(static_cast<RegisterId>(index));
}

Result<Immediate> parse_immediate_decimal(const std::string_view text) {
    if (text == "0") {
        return Result<Immediate>::success(Immediate{Integer{0}});
    }

    const bool negative = !text.empty() && text.front() == '-';
    const std::string_view magnitude = negative ? text.substr(1) : text;
    if (!has_canonical_unsigned_syntax(magnitude) || magnitude == "0") {
        return Result<Immediate>::failure(invalid_decimal(
            DiagnosticCode::invalid_immediate_syntax,
            "un inmediato debe cumplir 0 o -?[1-9][0-9]*"));
    }

    Integer value = parse_unsigned_digits(magnitude);
    if (negative) {
        value = -value;
    }
    return Result<Immediate>::success(Immediate{std::move(value)});
}

Result<MemorySize> parse_memory_size_decimal(const std::string_view text) {
    if (!has_canonical_unsigned_syntax(text)) {
        return Result<MemorySize>::failure(invalid_decimal(
            DiagnosticCode::invalid_memory_size_syntax,
            "MEM requiere un entero decimal no negativo en forma canonica"));
    }

    return Result<MemorySize>::success(
        MemorySize{parse_unsigned_digits(text)});
}

Result<MemorySlot> parse_memory_slot_decimal(const std::string_view text) {
    if (!has_canonical_unsigned_syntax(text)) {
        return Result<MemorySlot>::failure(invalid_decimal(
            DiagnosticCode::invalid_slot_syntax,
            "un indice de memoria debe ser decimal no negativo y canonico"));
    }

    return Result<MemorySlot>::success(
        MemorySlot{parse_unsigned_digits(text)});
}

std::string to_decimal_string(const Integer& value) {
    // convert_to<string> serializa cpp_int directamente; no estrecha primero el
    // numero y por eso conserva exactamente todos sus digitos y su signo.
    return value.convert_to<std::string>();
}

}  // namespace lcero::vm
