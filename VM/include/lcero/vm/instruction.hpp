#pragma once

#include "lcero/vm/diagnostics.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lcero::vm {

// Todos los valores numericos de la RI usan cpp_int. Esto evita reducir la
// semantica matematica de los enteros a la capacidad accidental de 64 bits.
using Integer = boost::multiprecision::cpp_int;

// El enum contiene exactamente los opcodes ejecutables aprobados. LABEL queda
// fuera porque es una directiva que el loader resolvera antes de formar P.
enum class Opcode {
    constant,
    load,
    store,
    move,
    add,
    subtract,
    multiply,
    divide,
    compare_equal,
    compare_not_equal,
    compare_less,
    compare_less_equal,
    compare_greater,
    compare_greater_equal,
    jump,
    jump_if_zero,
    print,
    halt,
};

enum class RegisterId : std::uint8_t {
    r0 = 0,
    r1 = 1,
    r2 = 2,
    r3 = 3,
};

// Estos envoltorios impiden confundir un inmediato, una cota MEM, un indice de
// slot y un destino ya resuelto, aunque todos tengan representacion numerica.
struct Immediate {
    Integer value;
};

struct MemorySize {
    Integer value;
};

struct MemorySlot {
    Integer index;
};

struct JumpTarget {
    std::size_t instruction_index;
};

using Operand =
    std::variant<RegisterId, MemorySlot, Immediate, JumpTarget>;

struct Instruction {
    Opcode opcode;
    std::vector<Operand> operands;
    // La linea pertenece al archivo .lci y se conserva para diagnosticos; no es
    // un token, nodo de AST ni estructura interna del compilador.
    std::size_t source_line;
};

// Program representa P. Solo ofrece lectura, de modo que la secuencia de
// instrucciones ejecutables permanece inmutable durante una ejecucion.
class Program {
public:
    Program() = default;
    explicit Program(std::vector<Instruction> instructions);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const Instruction& at(std::size_t index) const;
    [[nodiscard]] const std::vector<Instruction>& instructions() const noexcept;

private:
    std::vector<Instruction> instructions_;
};

[[nodiscard]] std::string_view opcode_name(Opcode opcode) noexcept;
[[nodiscard]] Result<RegisterId> make_register_id(std::uint32_t index);

// Las tres funciones son deliberadamente distintas: el inmediato admite signo
// negativo; MEM y los indices son naturales escritos en forma canonica.
[[nodiscard]] Result<Immediate> parse_immediate_decimal(std::string_view text);
[[nodiscard]] Result<MemorySize> parse_memory_size_decimal(std::string_view text);
[[nodiscard]] Result<MemorySlot> parse_memory_slot_decimal(std::string_view text);
[[nodiscard]] std::string to_decimal_string(const Integer& value);

}  // namespace lcero::vm
