#include "lcero/vm/diagnostics.hpp"
#include "lcero/vm/execution_state.hpp"
#include "lcero/vm/instruction.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using lcero::vm::DiagnosticCode;
using lcero::vm::ExecutionState;
using lcero::vm::Immediate;
using lcero::vm::Instruction;
using lcero::vm::Integer;
using lcero::vm::MachineStatus;
using lcero::vm::MemorySize;
using lcero::vm::MemorySlot;
using lcero::vm::Opcode;
using lcero::vm::Operand;
using lcero::vm::Program;
using lcero::vm::RegisterId;

int failures = 0;

void expect(const bool condition, const char* description) {
    if (condition) {
        std::cout << "[OK] " << description << '\n';
        return;
    }

    ++failures;
    std::cerr << "[FALLO] " << description << '\n';
}

template <typename T>
bool has_error_code(const lcero::vm::Result<T>& result,
                    const DiagnosticCode code) {
    return !result.ok() && result.diagnostic.has_value() &&
           result.diagnostic->code == code;
}

bool has_error_code(const lcero::vm::OperationResult& result,
                    const DiagnosticCode code) {
    return !result.ok() && result.diagnostic.has_value() &&
           result.diagnostic->code == code;
}

ExecutionState make_state(Program program, const Integer& memory_size) {
    auto created = ExecutionState::create(
        std::move(program), MemorySize{memory_size});
    expect(created.ok(), "se crea un estado con MEM no negativa");
    return std::move(*created.value);
}

void test_decimal_conversions() {
    const std::string positive_text =
        "123456789012345678901234567890123456789012345678901234567890"
        "12345678901234567890123456789012345678901234567890";
    const std::string negative_text = "-" + positive_text;

    const auto positive = lcero::vm::parse_immediate_decimal(positive_text);
    const auto negative = lcero::vm::parse_immediate_decimal(negative_text);
    expect(positive.ok(), "se analiza un inmediato de mas de 100 digitos");
    expect(negative.ok(), "se analiza el mismo inmediato con signo negativo");
    expect(lcero::vm::to_decimal_string(positive.value->value) == positive_text,
           "el entero grande positivo vuelve a decimal sin perdida");
    expect(lcero::vm::to_decimal_string(negative.value->value) == negative_text,
           "el entero grande negativo vuelve a decimal sin perdida");

    // La negacion siguiente es una operacion interna sobre el dominio entero;
    // no crea ni sugiere un opcode NEG, que no pertenece a la ISA aprobada.
    const Integer internally_negated = -positive.value->value;
    expect(internally_negated == negative.value->value,
           "la negacion interna de cpp_int conserva todos los digitos");

    expect(has_error_code(lcero::vm::parse_immediate_decimal("-0"),
                          DiagnosticCode::invalid_immediate_syntax),
           "-0 no pertenece a la sintaxis canonica del inmediato");
    expect(has_error_code(lcero::vm::parse_immediate_decimal("01"),
                          DiagnosticCode::invalid_immediate_syntax),
           "un inmediato no admite ceros iniciales");
    expect(has_error_code(lcero::vm::parse_immediate_decimal("+1"),
                          DiagnosticCode::invalid_immediate_syntax),
           "un inmediato positivo no admite signo mas");

    expect(lcero::vm::parse_memory_size_decimal("0").ok(),
           "MEM acepta la cota cero");
    expect(has_error_code(lcero::vm::parse_memory_size_decimal("-1"),
                          DiagnosticCode::invalid_memory_size_syntax),
           "MEM rechaza sintacticamente valores negativos");
    expect(has_error_code(lcero::vm::parse_memory_slot_decimal("02"),
                          DiagnosticCode::invalid_slot_syntax),
           "un indice de slot exige forma decimal canonica");
}

void test_typed_program_and_initial_state() {
    const auto immediate = lcero::vm::parse_immediate_decimal("7");
    std::vector<Instruction> instructions;
    instructions.push_back(Instruction{
        Opcode::constant,
        std::vector<Operand>{RegisterId::r0, *immediate.value},
        3U});
    instructions.push_back(
        Instruction{Opcode::halt, std::vector<Operand>{}, 4U});

    ExecutionState state = make_state(Program(std::move(instructions)), 2);
    expect(state.program().size() == 2U,
           "P contiene solo las instrucciones ejecutables tipadas");
    expect(state.program().at(0).opcode == Opcode::constant,
           "P conserva el opcode sin volver a parsear una cadena");
    expect(state.program().at(0).source_line == 3U,
           "la instruccion conserva su linea .lci para diagnosticos");
    expect(state.program_counter() == 0U, "pc comienza en cero");
    expect(state.executed_steps() == 0U, "k comienza en cero");
    expect(state.status() == MachineStatus::running,
           "el estado comienza sin resultado terminal");
    expect(state.initialized_slot_count() == 0U,
           "M comienza sin slots materializados");
    expect(state.output_values().empty(), "O comienza como secuencia vacia");
}

void test_registers_and_value_copies() {
    ExecutionState state = make_state(Program{}, 0);

    const auto absent = state.read_register(RegisterId::r0);
    expect(has_error_code(absent, DiagnosticCode::uninitialized_register),
           "leer R0 antes de asignarlo informa no inicializado");

    expect(state.write_register(RegisterId::r0, Integer{0}).ok(),
           "se puede escribir el valor cero en R0");
    const auto zero = state.read_register(RegisterId::r0);
    expect(zero.ok() && *zero.value == 0,
           "R0=0 se distingue de un registro ausente");

    const auto invalid =
        state.read_register(static_cast<RegisterId>(4U));
    expect(has_error_code(invalid, DiagnosticCode::invalid_register),
           "un registro fuera de R0..R3 se distingue de uno no inicializado");

    const auto parsed = lcero::vm::parse_immediate_decimal(
        "999999999999999999999999999999999999999999999999999999999999"
        "99999999999999999999999999999999999999999999999999999");
    expect(state.write_register(RegisterId::r1, parsed.value->value).ok(),
           "R1 almacena un cpp_int de mas de 100 digitos");
    auto copied = state.read_register(RegisterId::r1);
    const Integer original = *copied.value;
    *copied.value += 1;
    const auto reread = state.read_register(RegisterId::r1);
    expect(*reread.value == original,
           "modificar una copia no altera el entero original del registro");
}

void test_sparse_memory_bounds_and_absence() {
    ExecutionState state = make_state(Program{}, 2);
    const MemorySlot slot0{0};
    const MemorySlot slot1{1};
    const MemorySlot slot2{2};

    expect(has_error_code(state.read_memory(slot0),
                          DiagnosticCode::uninitialized_slot),
           "leer el slot 0 vacio informa ausencia, no valor cero");
    expect(state.write_memory(slot0, Integer{0}).ok(),
           "MEM 2 permite almacenar cero en el slot 0");
    const auto stored_zero = state.read_memory(slot0);
    expect(stored_zero.ok() && *stored_zero.value == 0,
           "el cero almacenado se recupera como valor presente");

    const Integer negative_large =
        lcero::vm::parse_immediate_decimal(
            "-123456789012345678901234567890123456789012345678901234567890"
            "12345678901234567890123456789012345678901234567890")
            .value->value;
    expect(state.write_memory(slot1, negative_large).ok(),
           "MEM 2 permite el indice superior valido 1");
    expect(*state.read_memory(slot1).value == negative_large,
           "la memoria dispersa recupera un entero negativo grande");
    expect(state.initialized_slot_count() == 2U,
           "solo los dos slots escritos se materializan fisicamente");

    expect(has_error_code(state.read_memory(slot2),
                          DiagnosticCode::slot_out_of_range),
           "el indice 2 queda fuera de MEM 2");
    expect(has_error_code(state.write_memory(MemorySlot{-1}, Integer{5}),
                          DiagnosticCode::slot_out_of_range),
           "un indice negativo queda fuera de rango");
}

void test_output_counters_and_terminal_states() {
    ExecutionState state = make_state(
        Program({Instruction{Opcode::halt, std::vector<Operand>{}, 1U}}), 0);
    state.append_output(Integer{8});
    state.append_output(Integer{-3});
    expect(state.output_text() == "8\n-3\n",
           "O conserva orden y serializa cada PRINT con LF");

    expect(state.set_program_counter(1U).ok(),
           "pc puede representar |P| para una etiqueta final resuelta");
    expect(has_error_code(state.set_program_counter(2U),
                          DiagnosticCode::program_counter_out_of_range),
           "pc no puede superar |P|");
    expect(state.record_executed_instruction().ok() &&
               state.executed_steps() == 1U,
           "k aumenta solo mediante el registro explicito de una instruccion");
    expect(state.terminate(MachineStatus::halt).ok(),
           "HALT es un estado terminal sin diagnostico");
    expect(has_error_code(state.record_executed_instruction(),
                          DiagnosticCode::invalid_terminal_transition),
           "k no cambia despues de alcanzar un estado terminal");
}

void test_negative_memory_size_condition() {
    const auto created =
        ExecutionState::create(Program{}, MemorySize{Integer{-1}});
    expect(has_error_code(created, DiagnosticCode::negative_memory_size),
           "una cota MEM negativa produce condicion de carga, no excepcion");
}

}  // namespace

int main() {
    test_decimal_conversions();
    test_typed_program_and_initial_state();
    test_registers_and_value_copies();
    test_sparse_memory_bounds_and_absence();
    test_output_counters_and_terminal_states();
    test_negative_memory_size_condition();

    if (failures == 0) {
        std::cout << "Todas las pruebas del modelo operacional pasaron.\n";
        return 0;
    }

    std::cerr << failures << " prueba(s) del modelo fallaron.\n";
    return 1;
}
