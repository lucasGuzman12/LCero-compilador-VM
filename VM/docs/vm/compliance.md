# Conformidad de la VM con `LCERO_IR 1`

## Frontera auditada

La máquina virtual recibe exclusivamente archivos `.lci` con encabezado `LCERO_IR 1`. El ejecutable no abre archivos `.lc`, no recibe tokens, AST ni estructuras privadas del compilador, y no vuelve a interpretar texto después de que el loader y el validador construyen instrucciones tipadas. Esta separación hace de la RI el contrato formal entre compilador y máquina.

La auditoría no encontró un módulo de compilador dentro del proyecto. Por ese motivo, la prueba futura `.lc -> compilador -> .lci -> VM` permanece pendiente y no se presenta como aprobada. Los 40 fixtures contractuales prueban la VM de manera independiente; además, seis `.lci` recibidos como salidas del compilador pasaron validación y ejecución en Debug y Release el 22/10/2026.

`LABEL` es una directiva resuelta a un índice 0-based de `P`; no es un opcode, no se almacena como instrucción ejecutable y no incrementa `k`. El encabezado y `MEM` tampoco pertenecen a `P`.

## Trazabilidad de la ISA

| Opcode | Semántica implementada | Prueba contractual principal |
|---|---|---|
| `CONST Rd,n` | Escribe el `cpp_int` inmediato exacto en `Rd` | `01_hola_registros`, `04_entero_grande` |
| `LOAD Rd,m` | Lee `M[m]`; falla si el slot no fue inicializado | `07_memoria_load_store`, `20_slot_sin_inicializar` |
| `STORE m,Rs` | Copia el valor inicializado de `Rs` en `M[m]` | `07_memoria_load_store`, `22_store_registro_no_inicializado` |
| `MOV Rd,Rs` | Copia el valor previo de `Rs` sin aliasar almacenamiento | `06_mov_alias` |
| `ADD Rd,Ra,Rb` | Suma matemática tras leer ambos operandos | `02_precedencia_compilada`, `suma_simple.lci` |
| `SUB Rd,Ra,Rb` | Resta matemática tras leer ambos operandos | `08_bucle_descendente` |
| `MUL Rd,Ra,Rb` | Multiplicación matemática de enteros arbitrarios | `02_precedencia_compilada`, `13_multiplicacion_bigint` |
| `DIV Rd,Ra,Rb` | División entera truncada hacia cero; divisor cero es `ERROR` dinámico | `03_division_entera_negativa`, `19_division_por_cero` |
| `CMP_EQ Rd,Ra,Rb` | Escribe exactamente 1 si `Ra = Rb`, en otro caso 0 | `05_comparadores` |
| `CMP_NE Rd,Ra,Rb` | Escribe exactamente 1 si `Ra != Rb`, en otro caso 0 | `05_comparadores` |
| `CMP_LT Rd,Ra,Rb` | Escribe exactamente 1 si `Ra < Rb`, en otro caso 0 | `05_comparadores` |
| `CMP_LE Rd,Ra,Rb` | Escribe exactamente 1 si `Ra <= Rb`, en otro caso 0 | `05_comparadores` |
| `CMP_GT Rd,Ra,Rb` | Escribe exactamente 1 si `Ra > Rb`, en otro caso 0 | `05_comparadores` |
| `CMP_GE Rd,Ra,Rb` | Escribe exactamente 1 si `Ra >= Rb`, en otro caso 0 | `05_comparadores` |
| `JMP label` | Sustituye `pc` por el destino resuelto, sin incremento adicional | `08_bucle_descendente`, `17_bucle_bajo_limite` |
| `JZ Rs,label` | Salta si `Rs` vale cero; si no, avanza una posición | `09_jz_tomado`, `10_jz_no_tomado`, `12_evitar_error_rama_no_ejecutada` |
| `PRINT Rs` | Acumula y emite el decimal seguido por un único byte LF | `01_hola_registros`, `08_bucle_descendente`, `24_salida_parcial_antes_error` |
| `HALT` | Termina con éxito y cuenta un paso | `14_halt_solo`, `15_halt_exacto_limite` |

Las pruebas C++ también cubren aliasing como `ADD R0,R0,R1`, enteros de más de 100 dígitos, memoria dispersa, registros ausentes y la estructura inicial `pc=0`, `k=0`.

## Superficies observables

| Interfaz | Garantía |
|---|---|
| `--check archivo.lci` | Carga y valida todo el archivo sin ejecutar instrucciones |
| ejecución posicional | Valida antes de iniciar y devuelve `HALT`, `ERROR` o `LIMIT` |
| `--max-steps N` | Comprueba `k >= N` antes de la siguiente instrucción |
| `--no-step-limit` | Desactiva el presupuesto solo por petición explícita |
| `--trace ruta` | Observa transiciones sin modificar `C`, `stdout` ni el código terminal |
| `--help`, `--version` | Modos informativos, separados de la ejecución de RI |

Las rutas se reciben como `std::filesystem::path` desde `wmain`, por lo que admiten rutas relativas al directorio de trabajo, absolutas, con espacios y Unicode. Durante la ejecución, `stdout` queda reservado a `PRINT`; estado y diagnósticos se escriben en `stderr`.

## Resultados exigidos y evidencia

La aceptación requiere, en Debug y Release:

- compilación MSVC x64 sin advertencias relevantes del proyecto;
- 6 pruebas CTest, incluida la suite contractual;
- distribución exacta de 40 fixtures: 15 `HALT`, 3 `LIMIT`, 6 `ERROR` y 16 `ERROR_DE_CARGA`;
- coincidencia byte a byte de `stdout`, sin insertar CR;
- ejemplos manuales de suma, bucle descendente y error dinámico.

Los resultados observados de la etapa 07 se registran después de su ejecución real en [`../progress.md`](../progress.md). Esta página define trazabilidad y criterios; no reemplaza esa evidencia.

## Limitaciones reales

- `cpp_int` modela enteros sin un límite fijo de bits, pero cada ejecución concreta está limitada por RAM y recursos del sistema.
- `MEM n` es una cota lógica representable con `cpp_int`; la memoria dispersa evita reservar `n` objetos, aunque los slots efectivamente escritos sí consumen recursos físicos.
- `LIMIT` significa que se agotó un presupuesto observado. No demuestra que la ejecución sea infinita ni decide el problema de la parada.
- La máquina tiene cuatro registros. La asignación, el spilling y algoritmos como Chaitin son responsabilidades del compilador; la VM solo ejecuta `LOAD` y `STORE` ya emitidos.
- La suite aporta evidencia sobre casos concretos y regresiones. No constituye una demostración constructiva de universalidad o completitud de Turing.
- No hay GUI, bytecode binario, generación de código nativo ni intérprete del lenguaje fuente `.lc`.
- No se implementó profiling en esta etapa. [`measurement-plan.md`](measurement-plan.md) es una propuesta, no un informe de resultados.

