# Contrato congelado `LCERO_IR 1`

Esta carpeta contiene casos de conformidad para la interfaz entre el compilador
y la VM independiente. Este archivo resume el contrato aprobado; no lo modifica.
La fuente normativa sigue siendo el Documento Maestro de L-Cero, capítulo 11.

## Archivo

- Codificación UTF-8; el texto de RI usa caracteres ASCII.
- Se permiten líneas vacías y no se permiten comentarios.
- La primera línea no vacía es `LCERO_IR 1`.
- La segunda línea no vacía es `MEM n`, con direcciones válidas `0..n-1`.
- Debe existir al menos una instrucción `HALT`.

## Léxico de operandos

- Registros: exactamente `R0`, `R1`, `R2`, `R3`.
- Enteros: `0` o `-?[1-9][0-9]*`; no se admiten `+`, ceros iniciales ni `-0`.
- Etiquetas: `[A-Za-z_][A-Za-z0-9_]*`, únicas y distintas de las mnemónicas.

## Instrucciones autorizadas

```text
CONST Rd, n
LOAD Rd, m
STORE m, Rs
MOV Rd, Rs
ADD Rd, Ra, Rb
SUB Rd, Ra, Rb
MUL Rd, Ra, Rb
DIV Rd, Ra, Rb
CMP_EQ Rd, Ra, Rb
CMP_NE Rd, Ra, Rb
CMP_LT Rd, Ra, Rb
CMP_LE Rd, Ra, Rb
CMP_GT Rd, Ra, Rb
CMP_GE Rd, Ra, Rb
JMP etiqueta
JZ Rs, etiqueta
LABEL etiqueta
PRINT Rs
HALT
```

Los operandos fuente de aritmética se leen antes de escribir el destino. `DIV`
trunca hacia cero y dividir por cero es un error dinámico. Las comparaciones
producen `0` o `1`. `PRINT` escribe el entero decimal y un salto de línea. Las
directivas y etiquetas no cuentan como pasos de ejecución.

Los archivos de `examples/` y `expected/` son vectores compartibles de contrato;
su ejecución queda a cargo de la VM, que vive en otro repositorio.
