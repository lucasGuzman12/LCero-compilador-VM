# Modelo operacional de la VM

## Alcance de la etapa 01

Esta etapa representa el estado matemático `C=(P,pc,R,M,O,k)` y sus condiciones de error, pero no carga archivos ni aplica la semántica de ningún opcode. En particular, construir una `Instruction` no equivale a ejecutarla.

## Correspondencia del modelo

| Componente | Representación | Propiedad preservada |
|---|---|---|
| `P` | `Program` con `vector<Instruction>` privado | Secuencia inmutable de instrucciones ejecutables, sin `LABEL`, encabezado ni `MEM` |
| `pc` | `size_t` | Índice 0-based dentro de `0..|P|` |
| `R` | `array<optional<cpp_int>,4>` | Exactamente `R0..R3`, inicialmente sin valor |
| `M` | cota `cpp_int` y `map<cpp_int,cpp_int>` | Memoria dispersa; solo existen físicamente los slots inicializados dentro de `0..n-1` |
| `O` | `vector<cpp_int>` | Salidas en orden, inspeccionables antes de escribirlas con LF |
| `k` | `uint64_t` | Cantidad de instrucciones registradas como efectivamente ejecutadas |

## Invariantes de transición

1. `P` no cambia durante una ejecución y solo contiene opcodes de la ISA autorizada.
2. `0 <= pc <= |P|`. El valor `|P|` puede surgir de una etiqueta al final; intentar ejecutar allí se resolverá como caída sin `HALT` en la fase del intérprete.
3. Cada registro está ausente o contiene un `cpp_int`; jamás se transforma ausencia en cero implícito.
4. Toda lectura o escritura de memoria verifica primero `0 <= m < MEM`. Una lectura en rango distingue un slot ausente de un slot que contiene cero.
5. `M` solo contiene claves válidas que fueron escritas. Su tamaño físico no define la cota lógica.
6. `O` solo crece al final y conserva el orden. Su renderizado decimal agrega exactamente `\n` por valor; no agrega CR ni traza.
7. `k` comienza en cero y crece una unidad por cada instrucción efectivamente registrada. Encabezado, `MEM` y `LABEL` no pertenecen a `P` y por tanto no pueden incrementar `k`.
8. Desde `RUNNING` se permite una única transición a `HALT`, `ERROR`, `ERROR_DE_CARGA` o `LIMIT`. No se modifica el estado después de terminar.
9. `ERROR` y `ERROR_DE_CARGA` conservan un diagnóstico; `HALT` y `LIMIT` no inventan uno.
10. Un error conocido del programa se expresa como resultado fallido y debe ser tratado por quien llama; no se usa una excepción sin capturar como semántica de la VM.

## Tipos de operandos

`RegisterId`, `MemorySlot`, `Immediate` y `JumpTarget` son tipos distintos. `Instruction` los conserva en un `variant`, junto con el `Opcode` y la línea de origen `.lci`. De esta forma, una etapa futura puede despachar sobre valores ya analizados sin volver a interpretar cadenas en cada paso.

La negación de un `cpp_int` usada por pruebas es una operación del lenguaje anfitrión para comprobar el dominio numérico; no introduce un opcode `NEG`.
