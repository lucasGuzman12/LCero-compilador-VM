# Loader y validador de LCERO_IR 1

## Frontera

`load_ir_source` realiza únicamente E/S, límites físicos, validación de bytes y separación de líneas. `validate_ir` reconoce la gramática de RI y produce `ValidatedIr`. Ninguna de las dos funciones crea una ejecución ni modifica `pc`, `R`, `M`, `O` o `k`.

```text
archivo .lci
    -> loader físico (ASCII, LF/CRLF, tamaño)
    -> primera pasada (cabeceras, tipos, LABEL -> pc)
    -> segunda pasada (referencias -> JumpTarget)
    -> Program + MemorySize
```

## Primera pasada

- Exige las dos cabeceras no vacías en orden.
- Reconoce únicamente los 18 opcodes autorizados y la directiva `LABEL`.
- Comprueba aridad y tipo de cada posición.
- Convierte inmediatos, cotas e índices directamente a `cpp_int`.
- Verifica cada slot contra la cota lógica `MEM` sin reservar el espacio completo.
- Asigna a cada etiqueta el tamaño actual de `P`; por ello las directivas no consumen índices.
- Registra la presencia sintáctica de al menos un `HALT`.

## Segunda pasada

Las referencias pendientes de `JMP` y `JZ` se buscan en la tabla de símbolos. Cada nombre se sustituye por `JumpTarget{pc}`. Las etiquetas hacia adelante y hacia atrás son equivalentes después de esta resolución. Una etiqueta final resuelve a `|P|`, que es válida estáticamente aunque alcanzarla pueda producir posteriormente una caída sin `HALT`.

## Separación estática/dinámica

`--check` acepta programas que pueden fallar o agotar su presupuesto al ejecutarse. Por ejemplo, una división por cero, un registro no inicializado o un bucle no son errores de carga si su texto y operandos son válidos. `LIMIT` tampoco demuestra infinitud.

Un error de carga siempre ocurre antes de ejecutar la primera instrucción y, por tanto, no produce `PRINT`, `STEPS` ni un estado `HALT`.
