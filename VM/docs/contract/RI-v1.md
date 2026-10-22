# LCERO_IR 1 — Guía de consulta de la VM (resumen, no reemplaza el documento maestro)

**Fuente normativa:** *L-Cero, Documento maestro de diseño y formalización, Hito 0*, versión de portada 1.1, 19/10/2026, capítulo 11. Contrato `LCERO_IR 1` versión 1.0 **APROBADO Y CONGELADO**. Si surge una contradicción, prevalece el documento original y se registra una solicitud de cambio; no se inventan instrucciones.

## Formato
- La VM recibe exclusivamente `.lci` (no fuente `.lc`). Texto UTF-8 limitado a caracteres ASCII; LF/CRLF; líneas en blanco permitidas; comentarios no permitidos.
- Primera línea no vacía: `LCERO_IR 1`; segunda línea no vacía: `MEM n`, `n` decimal no negativo. Exactamente una instrucción o `LABEL nombre` por línea. Mnemónicos mayúsculas.
- Registros `R0..R3`, inicialmente sin valor. Memoria lógica: slots `0..n-1`, inicialmente sin valor; `MEM` incluye slots usados para spilling.
- Etiquetas `[A-Za-z_][A-Za-z0-9_]*`, únicas, sin palabras reservadas de instrucciones. Resolver a índices de instrucción 0-based; una etiqueta al final puede apuntar a `|P|`.
- Inmediato: `0` o `-?[1-9][0-9]*`; `-0`, `+1` y `01` no son inmediatos válidos.
- Se exige la presencia de al menos un `HALT`, no necesariamente alcanzable. EOF no finaliza normalmente por sí solo.

## ISA aprobada
| Instrucción | Operación |
|---|---|
| `CONST Rd, n` | `Rd ← n` |
| `LOAD Rd, m` | `Rd ← M[m]`; requiere slot inicializado |
| `STORE m, Rs` | `M[m] ← Rs`; requiere Rs inicializado |
| `MOV Rd, Rs` | `Rd ← Rs`; requiere Rs inicializado |
| `ADD Rd, Ra, Rb` | suma de ambos registros |
| `SUB Rd, Ra, Rb` | resta |
| `MUL Rd, Ra, Rb` | multiplicación |
| `DIV Rd, Ra, Rb` | división entera truncada hacia cero; divisor ≠0 |
| `CMP_EQ/NE/LT/LE/GT/GE Rd, Ra, Rb` | comparación, resultado 1/0 |
| `JMP etiqueta` | salto incondicional |
| `JZ Rs, etiqueta` | salto si Rs=0, si no pc++ |
| `PRINT Rs` | escribe decimal de Rs seguido de LF |
| `HALT` | finalización normal |

No `NEG` ni otras instrucciones. En operaciones de 3 registros, leer orígenes desde el estado anterior a sobrescribir el destino.

## Estado y semántica
`C=(P,pc,R,M,O,k)`; `P` inmutable; `pc` 0-based, `R` y `M` funciones parciales, `O` secuencia de salidas, `k` instrucciones ejecutadas. `LABEL`, `MEM` y el encabezado no están en `P` y no cuentan pasos. Instrucción normal => pc+1; `JMP` y `JZ` siguen sus saltos; `HALT` se contabiliza y termina. Si existe presupuesto N, antes de ejecutar cada siguiente instrucción se comprueba `k>=N`; resultado `LIMIT` con salida parcial, jamás prueba de bucle infinito. Si se ejecutó HALT y entonces k==N, es `HALT`.

## Estados
- `HALT`: terminación normal y salida O.
- `ERROR`: error dinámico como división por cero, lectura no inicializada o caída sin HALT, con salida parcial y diagnóstico.
- `ERROR_DE_CARGA`: formato/versiones/opcodes/registros/slots/referencias inválidos; ninguna instrucción ejecutada.
- `LIMIT`: presupuesto de instrucciones agotado, inconcluso.

**Sin congelar en contrato:** sintaxis CLI, códigos numéricos de salida del proceso, JSON de diagnósticos, ruta de archivos, política detallada de fallos físicos, interpretación de algunos detalles de normalización de `MEM`/slots y de conteo de intentos fallidos. Esas decisiones se documentan en `docs/decisions.md` sin presentar convenciones nuevas como parte de la ISA.
