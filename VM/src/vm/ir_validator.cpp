#include "lcero/vm/ir_validator.hpp"

#include <array>
#include <cstddef>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <map>
#include <utility>
#include <variant>
#include <vector>

namespace lcero::vm {
namespace {

enum class OperandKind {
    register_id,
    memory_slot,
    immediate,
    label_reference,
};

struct OpcodeSpec {
    std::string_view mnemonic;
    Opcode opcode;
    std::array<OperandKind, 3> operands;
    std::size_t arity;
};

// Esta tabla es la gramatica superficial completa de las instrucciones de RI.
// No contiene acciones de ejecucion y no reconoce tokens del lenguaje .lc.
constexpr std::array<OpcodeSpec, 18> kOpcodeSpecs{{
    {"CONST", Opcode::constant,
     {OperandKind::register_id, OperandKind::immediate,
      OperandKind::register_id}, 2U},
    {"LOAD", Opcode::load,
     {OperandKind::register_id, OperandKind::memory_slot,
      OperandKind::register_id}, 2U},
    {"STORE", Opcode::store,
     {OperandKind::memory_slot, OperandKind::register_id,
      OperandKind::register_id}, 2U},
    {"MOV", Opcode::move,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 2U},
    {"ADD", Opcode::add,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"SUB", Opcode::subtract,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"MUL", Opcode::multiply,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"DIV", Opcode::divide,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"CMP_EQ", Opcode::compare_equal,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"CMP_NE", Opcode::compare_not_equal,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"CMP_LT", Opcode::compare_less,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"CMP_LE", Opcode::compare_less_equal,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"CMP_GT", Opcode::compare_greater,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"CMP_GE", Opcode::compare_greater_equal,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 3U},
    {"JMP", Opcode::jump,
     {OperandKind::label_reference, OperandKind::register_id,
      OperandKind::register_id}, 1U},
    {"JZ", Opcode::jump_if_zero,
     {OperandKind::register_id, OperandKind::label_reference,
      OperandKind::register_id}, 2U},
    {"PRINT", Opcode::print,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 1U},
    {"HALT", Opcode::halt,
     {OperandKind::register_id, OperandKind::register_id,
      OperandKind::register_id}, 0U},
}};

using PendingOperand =
    std::variant<RegisterId, MemorySlot, Immediate, std::string>;

struct PendingInstruction {
    Opcode opcode;
    std::vector<PendingOperand> operands;
    std::size_t source_line;
};

struct FirstPass {
    MemorySize memory_size;
    std::vector<PendingInstruction> instructions;
    std::map<std::string, std::size_t> labels;
    bool has_halt = false;
};

bool is_horizontal_space(const char character) noexcept {
    return character == ' ' || character == '\t';
}

std::string_view trim(const std::string_view text) noexcept {
    std::size_t begin = 0U;
    while (begin < text.size() && is_horizontal_space(text[begin])) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && is_horizontal_space(text[end - 1U])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

Diagnostic load_error(const DiagnosticCode code,
                      std::string message,
                      const std::optional<std::size_t> line = std::nullopt) {
    return {DiagnosticCategory::load, code, std::move(message), line};
}

Diagnostic resource_error(const std::string& message) {
    return {DiagnosticCategory::resource,
            DiagnosticCode::resource_exhausted, message};
}

const OpcodeSpec* find_opcode_spec(const std::string_view mnemonic) noexcept {
    for (const OpcodeSpec& spec : kOpcodeSpecs) {
        if (spec.mnemonic == mnemonic) {
            return &spec;
        }
    }
    return nullptr;
}

bool is_label_identifier(const std::string_view text) noexcept {
    if (text.empty()) {
        return false;
    }

    const auto is_letter = [](const char character) {
        return (character >= 'A' && character <= 'Z') ||
               (character >= 'a' && character <= 'z');
    };
    const auto is_digit = [](const char character) {
        return character >= '0' && character <= '9';
    };

    if (!is_letter(text.front()) && text.front() != '_') {
        return false;
    }
    for (const char character : text.substr(1U)) {
        if (!is_letter(character) && !is_digit(character) &&
            character != '_') {
            return false;
        }
    }
    return true;
}

struct HeadAndRemainder {
    std::string_view head;
    std::string_view remainder;
    bool comma_after_head;
};

HeadAndRemainder split_head(const std::string_view line) noexcept {
    std::size_t end = 0U;
    while (end < line.size() && !is_horizontal_space(line[end]) &&
           line[end] != ',') {
        ++end;
    }

    const bool comma_after_head = end < line.size() && line[end] == ',';
    std::size_t remainder_begin = end;
    while (remainder_begin < line.size() &&
           is_horizontal_space(line[remainder_begin])) {
        ++remainder_begin;
    }
    return {line.substr(0U, end), line.substr(remainder_begin),
            comma_after_head};
}

Result<std::vector<std::string_view>> split_operands(
    const std::string_view remainder,
    const std::size_t expected_arity,
    const std::size_t line) {
    std::vector<std::string_view> operands;

    if (expected_arity == 0U) {
        if (!remainder.empty()) {
            return Result<std::vector<std::string_view>>::failure(load_error(
                DiagnosticCode::invalid_arity,
                "la instruccion no admite operandos", line));
        }
        return Result<std::vector<std::string_view>>::success(
            std::move(operands));
    }

    if (remainder.empty()) {
        return Result<std::vector<std::string_view>>::failure(load_error(
            DiagnosticCode::invalid_arity,
            "faltan operandos para la instruccion", line));
    }

    std::size_t begin = 0U;
    while (begin <= remainder.size()) {
        const std::size_t comma = remainder.find(',', begin);
        const std::size_t end =
            comma == std::string_view::npos ? remainder.size() : comma;
        const std::string_view operand =
            trim(remainder.substr(begin, end - begin));
        if (operand.empty()) {
            return Result<std::vector<std::string_view>>::failure(load_error(
                DiagnosticCode::invalid_arity,
                "una coma no puede dejar un operando vacio", line));
        }
        for (const char character : operand) {
            if (is_horizontal_space(character)) {
                return Result<std::vector<std::string_view>>::failure(load_error(
                    DiagnosticCode::invalid_operand,
                    "no se permiten espacios dentro de un operando", line));
            }
        }
        operands.push_back(operand);

        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1U;
    }

    if (operands.size() != expected_arity) {
        return Result<std::vector<std::string_view>>::failure(load_error(
            DiagnosticCode::invalid_arity,
            "cantidad de operandos distinta de la definida por la ISA", line));
    }
    return Result<std::vector<std::string_view>>::success(
        std::move(operands));
}

Result<RegisterId> parse_register(const std::string_view text,
                                  const std::size_t line) {
    if (text.size() == 2U && text.front() == 'R' &&
        text[1] >= '0' && text[1] <= '9') {
        auto parsed = make_register_id(
            static_cast<std::uint32_t>(text[1] - '0'));
        if (!parsed.ok()) {
            parsed.diagnostic->source_line = line;
        }
        return parsed;
    }

    return Result<RegisterId>::failure(load_error(
        text.empty() || text.front() != 'R'
            ? DiagnosticCode::invalid_operand
            : DiagnosticCode::invalid_register,
        "se esperaba uno de los registros R0, R1, R2 o R3", line));
}

template <typename T>
Result<T> attach_line(Result<T> parsed, const std::size_t line) {
    if (!parsed.ok() && parsed.diagnostic.has_value()) {
        parsed.diagnostic->source_line = line;
    }
    return parsed;
}

Result<PendingOperand> parse_operand(const OperandKind kind,
                                     const std::string_view text,
                                     const MemorySize& memory_size,
                                     const std::size_t line) {
    switch (kind) {
        case OperandKind::register_id: {
            auto parsed = parse_register(text, line);
            if (!parsed.ok()) {
                return Result<PendingOperand>::failure(*parsed.diagnostic);
            }
            return Result<PendingOperand>::success(*parsed.value);
        }
        case OperandKind::memory_slot: {
            auto parsed = attach_line(parse_memory_slot_decimal(text), line);
            if (!parsed.ok()) {
                return Result<PendingOperand>::failure(*parsed.diagnostic);
            }
            if (parsed.value->index >= memory_size.value) {
                return Result<PendingOperand>::failure(load_error(
                    DiagnosticCode::slot_out_of_range,
                    "el slot debe cumplir 0 <= indice < MEM", line));
            }
            return Result<PendingOperand>::success(*parsed.value);
        }
        case OperandKind::immediate: {
            auto parsed = attach_line(parse_immediate_decimal(text), line);
            if (!parsed.ok()) {
                return Result<PendingOperand>::failure(*parsed.diagnostic);
            }
            return Result<PendingOperand>::success(*parsed.value);
        }
        case OperandKind::label_reference:
            if (!is_label_identifier(text)) {
                return Result<PendingOperand>::failure(load_error(
                    DiagnosticCode::invalid_label,
                    "la referencia no cumple [A-Za-z_][A-Za-z0-9_]*",
                    line));
            }
            return Result<PendingOperand>::success(std::string(text));
    }

    return Result<PendingOperand>::failure(load_error(
        DiagnosticCode::invalid_operand,
        "tipo de operando interno desconocido", line));
}

Result<MemorySize> parse_memory_declaration(const SourceLine& source_line) {
    const std::string_view line = trim(source_line.text);
    const HeadAndRemainder parts = split_head(line);
    if (parts.comma_after_head || parts.head != "MEM" ||
        parts.remainder.empty()) {
        return Result<MemorySize>::failure(load_error(
            DiagnosticCode::missing_memory_declaration,
            "la segunda linea no vacia debe ser MEM n", source_line.number));
    }
    for (const char character : parts.remainder) {
        if (is_horizontal_space(character) || character == ',') {
            return Result<MemorySize>::failure(load_error(
                DiagnosticCode::invalid_memory_size_syntax,
                "MEM requiere un unico decimal no negativo", source_line.number));
        }
    }
    return attach_line(parse_memory_size_decimal(parts.remainder),
                       source_line.number);
}

OperationResult parse_label_definition(const std::string_view remainder,
                                       const std::size_t line,
                                       FirstPass& first_pass) {
    if (remainder.empty() || remainder.find(',') != std::string_view::npos) {
        return OperationResult::failure(load_error(
            DiagnosticCode::invalid_label,
            "LABEL requiere exactamente un identificador", line));
    }
    for (const char character : remainder) {
        if (is_horizontal_space(character)) {
            return OperationResult::failure(load_error(
                DiagnosticCode::invalid_label,
                "no se permiten espacios dentro de una etiqueta", line));
        }
    }
    if (!is_label_identifier(remainder)) {
        return OperationResult::failure(load_error(
            DiagnosticCode::invalid_label,
            "la etiqueta no cumple [A-Za-z_][A-Za-z0-9_]*", line));
    }
    if (find_opcode_spec(remainder) != nullptr) {
        return OperationResult::failure(load_error(
            DiagnosticCode::reserved_label,
            "una etiqueta no puede usar un mnemonic de la ISA", line));
    }

    const std::string name(remainder);
    const auto inserted =
        first_pass.labels.emplace(name, first_pass.instructions.size());
    if (!inserted.second) {
        return OperationResult::failure(load_error(
            DiagnosticCode::duplicate_label,
            "la etiqueta ya fue definida anteriormente", line));
    }
    return OperationResult::success();
}

OperationResult parse_body_line(const SourceLine& source_line,
                                FirstPass& first_pass) {
    const std::string_view line = trim(source_line.text);
    const HeadAndRemainder parts = split_head(line);
    if (parts.head.empty() || parts.comma_after_head) {
        return OperationResult::failure(load_error(
            DiagnosticCode::invalid_instruction_format,
            "se esperaba un mnemonic seguido de operandos", source_line.number));
    }

    if (parts.head == "LABEL") {
        return parse_label_definition(parts.remainder, source_line.number,
                                      first_pass);
    }

    const OpcodeSpec* spec = find_opcode_spec(parts.head);
    if (spec == nullptr) {
        return OperationResult::failure(load_error(
            DiagnosticCode::unknown_opcode,
            "el mnemonic no pertenece a la ISA LCERO_IR 1",
            source_line.number));
    }

    auto split = split_operands(parts.remainder, spec->arity,
                                source_line.number);
    if (!split.ok()) {
        return OperationResult::failure(*split.diagnostic);
    }

    PendingInstruction instruction{spec->opcode, {}, source_line.number};
    instruction.operands.reserve(spec->arity);
    for (std::size_t index = 0; index < spec->arity; ++index) {
        auto operand = parse_operand(spec->operands[index],
                                     (*split.value)[index],
                                     first_pass.memory_size,
                                     source_line.number);
        if (!operand.ok()) {
            return OperationResult::failure(*operand.diagnostic);
        }
        instruction.operands.push_back(std::move(*operand.value));
    }

    if (spec->opcode == Opcode::halt) {
        first_pass.has_halt = true;
    }
    first_pass.instructions.push_back(std::move(instruction));
    return OperationResult::success();
}

Result<FirstPass> perform_first_pass(const IrSource& source) {
    const SourceLine* header_line = nullptr;
    const SourceLine* memory_line = nullptr;
    std::size_t body_begin = source.lines.size();

    for (std::size_t index = 0; index < source.lines.size(); ++index) {
        if (trim(source.lines[index].text).empty()) {
            continue;
        }
        if (header_line == nullptr) {
            header_line = &source.lines[index];
            continue;
        }
        memory_line = &source.lines[index];
        body_begin = index + 1U;
        break;
    }

    if (header_line == nullptr || trim(header_line->text) != "LCERO_IR 1") {
        return Result<FirstPass>::failure(load_error(
            DiagnosticCode::invalid_header,
            "la primera linea no vacia debe ser exactamente LCERO_IR 1",
            header_line == nullptr
                ? std::optional<std::size_t>{}
                : std::optional<std::size_t>{header_line->number}));
    }
    if (memory_line == nullptr) {
        return Result<FirstPass>::failure(load_error(
            DiagnosticCode::missing_memory_declaration,
            "falta la segunda linea no vacia MEM n"));
    }

    auto memory_size = parse_memory_declaration(*memory_line);
    if (!memory_size.ok()) {
        return Result<FirstPass>::failure(*memory_size.diagnostic);
    }

    FirstPass first_pass{*memory_size.value, {}, {}, false};
    for (std::size_t index = body_begin; index < source.lines.size(); ++index) {
        if (trim(source.lines[index].text).empty()) {
            continue;
        }
        const OperationResult parsed =
            parse_body_line(source.lines[index], first_pass);
        if (!parsed.ok()) {
            return Result<FirstPass>::failure(*parsed.diagnostic);
        }
    }

    if (!first_pass.has_halt) {
        return Result<FirstPass>::failure(load_error(
            DiagnosticCode::missing_halt,
            "el archivo debe contener al menos una instruccion HALT"));
    }
    return Result<FirstPass>::success(std::move(first_pass));
}

Result<ValidatedIr> perform_second_pass(FirstPass first_pass) {
    std::vector<Instruction> instructions;
    instructions.reserve(first_pass.instructions.size());

    for (PendingInstruction& pending : first_pass.instructions) {
        std::vector<Operand> resolved;
        resolved.reserve(pending.operands.size());
        for (PendingOperand& operand : pending.operands) {
            if (const auto* label = std::get_if<std::string>(&operand)) {
                const auto found = first_pass.labels.find(*label);
                if (found == first_pass.labels.end()) {
                    return Result<ValidatedIr>::failure(load_error(
                        DiagnosticCode::unknown_label,
                        "el salto referencia una etiqueta no definida",
                        pending.source_line));
                }
                // La direccion es un indice 0-based de P. Una etiqueta al final
                // puede resolver legalmente al valor |P|.
                resolved.push_back(JumpTarget{found->second});
            } else if (const auto* reg = std::get_if<RegisterId>(&operand)) {
                resolved.push_back(*reg);
            } else if (const auto* slot = std::get_if<MemorySlot>(&operand)) {
                resolved.push_back(*slot);
            } else if (const auto* immediate =
                           std::get_if<Immediate>(&operand)) {
                resolved.push_back(*immediate);
            }
        }
        instructions.push_back(Instruction{
            pending.opcode, std::move(resolved), pending.source_line});
    }

    return Result<ValidatedIr>::success(
        ValidatedIr{Program(std::move(instructions)),
                    std::move(first_pass.memory_size),
                    std::move(first_pass.labels)});
}

}  // namespace

Result<ValidatedIr> validate_ir(const IrSource& source) {
    try {
        auto first_pass = perform_first_pass(source);
        if (!first_pass.ok()) {
            return Result<ValidatedIr>::failure(*first_pass.diagnostic);
        }
        return perform_second_pass(std::move(*first_pass.value));
    } catch (const std::bad_alloc&) {
        return Result<ValidatedIr>::failure(resource_error(
            "memoria insuficiente durante la validacion de la RI"));
    } catch (const std::length_error&) {
        return Result<ValidatedIr>::failure(resource_error(
            "la RI excede la capacidad representable del proceso"));
    }
}

}  // namespace lcero::vm
