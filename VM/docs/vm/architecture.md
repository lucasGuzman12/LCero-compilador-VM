# Arquitectura de la máquina virtual L-Cero

## Alcance y frontera

Este ejecutable implementa exclusivamente la máquina virtual de `LCERO_IR 1`. Su única entrada de programa es un archivo `.lci`; no recibe `.lc`, AST, tokens ni objetos internos del compilador. El capítulo 11 del Documento Maestro es la fuente normativa y [`docs/contract/RI-v1.md`](../contract/RI-v1.md) es un resumen instalado para consulta.

La separación es deliberada: el compilador decide cómo traducir el lenguaje fuente, asignar registros y realizar spilling; la VM valida y ejecuta el resultado intermedio sin reescribirlo.

## Flujo de datos

```text
ruta .lci
   │
   ▼
CLI / main ──► IR loader ──► IR validator ──► ValidatedIr
                  │                │              │
            bytes y líneas    sintaxis/tipos      ├─ Program P inmutable
            UTF-8/ASCII       LABEL → pc          ├─ MEM n
                                                └─ tabla de etiquetas
                                                     │
                                                     ▼
                                              Interpreter
                                                     │
                                                     ▼
                                        ExecutionState C=(P,pc,R,M,O,k)
                                            │                  │
                                            ├─ stdout: PRINT   └─ stderr: estado
                                            └─ trace opcional: diagnóstico
```

`--check` termina después del validador. La ejecución normal solo comienza si la totalidad del archivo fue cargada y validada correctamente.

## Responsabilidades por módulo

| Módulo | Archivos principales | Responsabilidad |
|---|---|---|
| Entrada de proceso | `src/vm/main.cpp` | `wmain`, argumentos Unicode y modo binario de `stdout` en Windows |
| CLI | `cli.hpp`, `cli.cpp` | opciones, rutas, códigos de salida, coordinación y canales observables |
| Loader | `ir_loader.hpp`, `ir_loader.cpp` | apertura binaria, cota física, bytes admitidos, LF/CRLF y líneas fuente |
| Validador | `ir_validator.hpp`, `ir_validator.cpp` | gramática superficial RI, operandos, `MEM`, dos pasadas de etiquetas y creación de `ValidatedIr` |
| Instrucciones | `instruction.hpp`, `instruction.cpp` | opcodes cerrados, operandos tipados, `Program` inmutable y conversión decimal exacta de `cpp_int` |
| Estado | `execution_state.hpp`, `execution_state.cpp` | `C=(P,pc,R,M,O,k)`, registros opcionales, memoria dispersa, salida y terminales |
| Intérprete | `interpreter.hpp`, `interpreter.cpp` | relación de transición de los 18 opcodes y presupuesto de pasos |
| Diagnósticos | `diagnostics.hpp`, `diagnostics.cpp` | categorías y códigos estables para carga, ejecución, recursos e invariantes internas |
| Trazas | `trace.hpp`, `trace.cpp` | observador `BEFORE`/`AFTER` por diferencias, sin modificar `C` ni `stdout` |

## Modelo de estado

`ExecutionState` materializa `C=(P,pc,R,M,O,k)`:

- `P` es una secuencia de instrucciones tipadas. Encabezado, `MEM` y `LABEL` no pertenecen a ella.
- `pc` es un índice 0-based y puede alcanzar `|P|` para diagnosticar caída sin `HALT`.
- `R` tiene exactamente cuatro `optional<cpp_int>`; ausencia y cero son estados distintos.
- `M` es `map<cpp_int,cpp_int>` con cota lógica `MEM n`. Solo se almacenan slots escritos.
- `O` conserva en orden los enteros de cada `PRINT`.
- `k` cuenta instrucciones completadas; una instrucción dinámicamente fallida no lo incrementa.

Las vistas públicas de registros, memoria, programa y salida son `const`. Esto permite inspección y trazas sin introducir una segunda vía de transición.

## Invariantes de ejecución

1. Toda ejecución recibe un `ValidatedIr`; un error de carga nunca crea transiciones.
2. Los operandos fuente se leen antes de escribir el destino, por lo que el aliasing de registros es válido.
3. Una instrucción completada incrementa `k` exactamente una vez; directivas nunca lo hacen.
4. `JMP` y `JZ` sustituyen `pc` sin incremento adicional cuando saltan.
5. El presupuesto N se consulta antes de la siguiente instrucción existente.
6. `HALT` cuenta un paso y conserva `pc` sobre sí mismo.
7. `P` no cambia durante la ejecución.

Las reglas formales están desarrolladas en [`operational-semantics.md`](operational-semantics.md).

## Errores y recursos

Los errores conocidos del programa son datos (`Result<T>`, `OperationResult` o un estado terminal), no excepciones sin tratar. Se distinguen:

- `ERROR_DE_CARGA`: versión, sintaxis, operandos, slots o etiquetas inválidos;
- `ERROR`: división por cero, lectura no inicializada o caída sin `HALT`;
- `LIMIT`: presupuesto agotado antes de la siguiente instrucción;
- fallos físicos: apertura, lectura, memoria o escritura, clasificados como recursos.

Los `std::bad_alloc` previsibles se capturan en las fronteras de carga, estado, transición y CLI. Los enteros son arbitrarios en el modelo, pero toda representación concreta continúa limitada por la RAM disponible.

## Targets y entrega

- `lcero_vm_core`: biblioteca estática del núcleo.
- `lcero_vm`: ejecutable cuyo nombre físico es `lcero-vm.exe`.
- `lcero_vm_tests`, `lcero_vm_model_tests`, `lcero_vm_loader_tests`, `lcero_vm_interpreter_tests`, `lcero_vm_trace_tests`: pruebas unitarias y de integración.
- `lcero_vm_contract_cli`: prueba CTest que inicia el ejecutable real sobre los 40 fixtures.

Los productos generados viven bajo `build/`, están excluidos de control de versiones y pueden regenerarse con CMake. No existe GUI, bytecode binario ni generador de código nativo.
