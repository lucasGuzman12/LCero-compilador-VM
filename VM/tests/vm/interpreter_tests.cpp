#include "lcero/vm/interpreter.hpp"
#include "lcero/vm/ir_loader.hpp"
#include "lcero/vm/ir_validator.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace {

using lcero::vm::DiagnosticCode;
using lcero::vm::ExecutionOptions;
using lcero::vm::ExecutionState;
using lcero::vm::MachineStatus;
using lcero::vm::Opcode;
using lcero::vm::Result;
using lcero::vm::ValidatedIr;

int failures = 0;

void expect(const bool condition, const std::string& description) {
    if (condition) {
        std::cout << "[OK] " << description << '\n';
        return;
    }
    ++failures;
    std::cerr << "[FALLO] " << description << '\n';
}

std::string read_binary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

Result<ValidatedIr> load_validated(const std::filesystem::path& path) {
    auto loaded = lcero::vm::load_ir_source(path);
    if (!loaded.ok()) {
        return Result<ValidatedIr>::failure(*loaded.diagnostic);
    }
    return lcero::vm::validate_ir(*loaded.value);
}

struct HaltCase {
    const char* id;
    const char* file_name;
    std::uint64_t expected_steps;
};

void test_all_valid_programs(const std::filesystem::path& contracts) {
    constexpr std::array<HaltCase, 14> cases{{
        {"01_hola_registros", "01_hola_registros.lci", 3},
        {"02_precedencia_compilada", "02_precedencia_compilada.lci", 7},
        {"03_division_entera_negativa", "03_division_entera_negativa.lci", 5},
        {"04_entero_grande", "04_entero_grande.lci", 3},
        {"05_comparadores", "05_comparadores.lci", 15},
        {"06_mov_alias", "06_mov_alias.lci", 5},
        {"07_memoria_load_store", "07_memoria_load_store.lci", 9},
        {"08_bucle_descendente", "08_bucle_descendente.lci", 40},
        {"09_jz_tomado", "09_jz_tomado.lci", 5},
        {"10_jz_no_tomado", "10_jz_no_tomado.lci", 6},
        {"11_spilling_manual", "11_spilling_manual.lci", 11},
        {"12_evitar_error_rama_no_ejecutada",
         "12_evitar_error_rama_no_ejecutada.lci", 5},
        {"13_multiplicacion_bigint", "13_multiplicacion_bigint.lci", 5},
        {"14_halt_solo", "14_halt_solo.lci", 1},
    }};

    std::set<Opcode> covered_opcodes;
    for (const HaltCase& test_case : cases) {
        const std::filesystem::path input =
            contracts / "valid" / test_case.file_name;
        auto validated = load_validated(input);
        expect(validated.ok(), std::string(test_case.id) + " carga y valida");
        if (!validated.ok()) {
            continue;
        }
        for (const auto& instruction :
             validated.value->program.instructions()) {
            covered_opcodes.insert(instruction.opcode);
        }

        // P ya no contiene directivas. interpret aplica una relacion de
        // transicion sobre instrucciones tipadas, no sobre lineas de texto.
        auto execution = lcero::vm::interpret(std::move(*validated.value));
        expect(execution.ok(), std::string(test_case.id) +
                                   " no produce fallo interno");
        if (!execution.ok()) {
            continue;
        }
        const ExecutionState& state = *execution.value;
        expect(state.status() == MachineStatus::halt,
               std::string(test_case.id) + " termina en HALT");
        expect(state.executed_steps() == test_case.expected_steps,
               std::string(test_case.id) + " conserva k esperado");
        const std::filesystem::path expected =
            contracts / "expected" /
            (std::string(test_case.id) + ".out");
        expect(state.output_text() == read_binary(expected),
               std::string(test_case.id) + " produce O exacta");
    }

    // El enum autorizado tiene dieciocho opcodes. Esta prueba impide que una
    // rama del interprete quede sin ser ejercitada por los casos de contrato.
    expect(covered_opcodes.size() == 18U,
           "los programas valid cubren los 18 opcodes ejecutables");
}

struct LimitCase {
    const char* id;
    const char* file_name;
    std::uint64_t max_steps;
    MachineStatus expected_status;
    std::uint64_t expected_steps;
};

void test_step_limits(const std::filesystem::path& contracts) {
    constexpr std::array<LimitCase, 4> cases{{
        {"15_halt_exacto_limite", "15_halt_exacto_limite.lci", 3,
         MachineStatus::halt, 3},
        {"16_limite_antes_halt", "16_limite_antes_halt.lci", 2,
         MachineStatus::limit, 2},
        {"17_bucle_bajo_limite", "17_bucle_bajo_limite.lci", 4,
         MachineStatus::limit, 4},
        {"18_presupuesto_cero", "18_presupuesto_cero.lci", 0,
         MachineStatus::limit, 0},
    }};

    for (const LimitCase& test_case : cases) {
        auto validated =
            load_validated(contracts / "limit" / test_case.file_name);
        expect(validated.ok(), std::string(test_case.id) + " carga y valida");
        if (!validated.ok()) {
            continue;
        }
        ExecutionOptions options;
        options.max_steps = test_case.max_steps;
        auto execution =
            lcero::vm::interpret(std::move(*validated.value), options);
        expect(execution.ok(), std::string(test_case.id) +
                                   " no produce fallo interno");
        if (!execution.ok()) {
            continue;
        }
        const ExecutionState& state = *execution.value;
        expect(state.status() == test_case.expected_status,
               std::string(test_case.id) + " alcanza el estado esperado");
        expect(state.executed_steps() == test_case.expected_steps,
               std::string(test_case.id) + " comprueba N antes del paso");
        const auto expected = contracts / "expected" /
                              (std::string(test_case.id) + ".out");
        expect(state.output_text() == read_binary(expected),
               std::string(test_case.id) + " preserva O");
    }

    // El fixture 17 se ejecuta exclusivamente con N=4. Alcanzar LIMIT prueba
    // agotamiento de ese presupuesto, no que el ciclo sea matematicamente infinito.
}

struct RuntimeCase {
    const char* id;
    const char* file_name;
    DiagnosticCode expected_code;
};

void test_runtime_errors(const std::filesystem::path& contracts) {
    constexpr std::array<RuntimeCase, 6> cases{{
        {"19_division_por_cero", "19_division_por_cero.lci",
         DiagnosticCode::division_by_zero},
        {"20_slot_sin_inicializar", "20_slot_sin_inicializar.lci",
         DiagnosticCode::uninitialized_slot},
        {"21_print_registro_no_inicializado",
         "21_print_registro_no_inicializado.lci",
         DiagnosticCode::uninitialized_register},
        {"22_store_registro_no_inicializado",
         "22_store_registro_no_inicializado.lci",
         DiagnosticCode::uninitialized_register},
        {"23_caida_sin_halt", "23_caida_sin_halt.lci",
         DiagnosticCode::fallthrough_without_halt},
        {"24_salida_parcial_antes_error",
         "24_salida_parcial_antes_error.lci",
         DiagnosticCode::division_by_zero},
    }};

    for (const RuntimeCase& test_case : cases) {
        auto validated = load_validated(
            contracts / "runtime_error" / test_case.file_name);
        expect(validated.ok(), std::string(test_case.id) +
                                   " es valido estaticamente");
        if (!validated.ok()) {
            continue;
        }
        auto execution = lcero::vm::interpret(std::move(*validated.value));
        expect(execution.ok(), std::string(test_case.id) +
                                   " se expresa como estado, no excepcion");
        if (!execution.ok()) {
            continue;
        }
        const ExecutionState& state = *execution.value;
        expect(state.status() == MachineStatus::error,
               std::string(test_case.id) + " termina en ERROR");
        expect(state.terminal_diagnostic().has_value() &&
                   state.terminal_diagnostic()->code == test_case.expected_code,
               std::string(test_case.id) + " distingue la causa dinamica");
        const auto expected = contracts / "expected" /
                              (std::string(test_case.id) + ".out");
        expect(state.output_text() == read_binary(expected),
               std::string(test_case.id) + " conserva salida parcial exacta");
    }
}

}  // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Uso de prueba: lcero_vm_interpreter_tests <contracts>\n";
        return 2;
    }
    const std::filesystem::path contracts(argv[1]);
    test_all_valid_programs(contracts);
    test_step_limits(contracts);
    test_runtime_errors(contracts);

    if (failures == 0) {
        std::cout << "Todas las pruebas del interprete pasaron.\n";
        return 0;
    }
    std::cerr << failures << " prueba(s) del interprete fallaron.\n";
    return 1;
}
