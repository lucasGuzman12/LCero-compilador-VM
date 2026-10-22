# Depuración paso a paso en Visual Studio Community 2026

La forma recomendada es **Abrir > Carpeta** sobre la raíz que contiene `CMakeLists.txt`. En la configuración comprobada, el generador creó `build/LCeroVM.slnx`, que también puede abrirse. Otras versiones pueden producir `.sln`; cualquiera de esas soluciones pertenece al árbol generado `build/`, puede recrearse y no debe versionarse.

Esta guía permite observar la configuración matemática `C=(P,pc,R,M,O,k)` durante una ejecución real. La traza y el depurador son observadores: no agregan instrucciones, no forman parte de `O` y no cambian la semántica de `LCERO_IR 1`.

## Preparar el proyecto CMake

1. Inicie Visual Studio Community 2026 desde un entorno que tenga `VCPKG_ROOT` definido.
2. Elija **Abrir > Carpeta** y seleccione `LCero`, la carpeta que contiene `CMakeLists.txt`.
3. Espere la configuración de CMake y seleccione el preset `vs2026-x64` y la configuración `x64-Debug`.
4. Compile el target `lcero_vm`.

La sesión gráfica no fue ejecutada automáticamente durante esta etapa; estos pasos documentan cómo reproducirla en el IDE instalado.

## Configurar una ejecución depurable

Seleccione `lcero_vm` como elemento de inicio y configure:

- Argumentos: `--trace build\debug-session.log test\contracts\valid\08_bucle_descendente.lci`
- Directorio de trabajo: la ruta absoluta de la carpeta `LCero`.

Las rutas relativas se resuelven respecto de ese directorio de trabajo. La ruta del log admite espacios y puede ser absoluta. No use una extensión `.lci` para la traza: ese sufijo se reserva a la representación intermedia de entrada.

## Breakpoint recomendado

Abra `src/vm/interpreter.cpp` y coloque un breakpoint dentro de `execute_instruction`, antes del `switch` sobre `instruction.opcode`. Esa función representa una transición individual. También puede detenerse en el cuerpo de `interpret`, justo antes de `execute_instruction`, para observar el estado anterior.

En el primer paso de `01_hola_registros.lci` se debe observar:

- `pc = 0` y `k = 0`;
- `instruction.opcode = Opcode::constant`;
- `instruction.source_line = 3`;
- los cuatro elementos de `state.registers()` como `std::nullopt`;
- `state.initialized_memory()` vacío.

Después de la transición, `R0` contiene el entero matemático 5, `pc=1` y `k=1`.

## Expresiones útiles en Watch

Los accesores son deliberadamente de solo lectura para que inspeccionar la VM no abra una vía de mutación:

```text
state.program_counter()
state.executed_steps()
state.status()
state.registers()
state.registers()[0]
state.registers()[1]
state.initialized_memory()
state.output_values()
state.program().size()
instruction.opcode
instruction.operands
instruction.source_line
previous_pc
```

`std::optional<cpp_int>` permite distinguir un registro ausente de uno que contiene cero. La memoria es `map<cpp_int,cpp_int>` y solo muestra slots escritos, aunque la cota lógica `MEM n` pueda ser mucho mayor.

La tabla de símbolos se conserva en `ValidatedIr::labels` antes de mover `program` al estado de ejecución. Cada entrada relaciona un nombre fuente con un índice ejecutable 0-based; `LABEL`, `MEM` y el encabezado no pertenecen a `P`.

## Leer el archivo de traza

El encabezado muestra el tamaño de `P`, la cota de memoria, la tabla de etiquetas y el estado inicial. Por cada instrucción hay:

- `BEFORE`: `k`, `pc`, índice ejecutable, línea `.lci`, opcode, operandos y registros antes de la transición;
- `AFTER`: estado posterior y un diff del registro, slot, salto, salida o terminación relevante;
- `TERMINAL`: `HALT`, `ERROR` o `LIMIT`, junto con `k`, `pc` y categoría.

El archivo se escribe en modo binario con bytes UTF-8/ASCII y LF. No es RI, no se puede ejecutar y nunca se mezcla con `stdout`.

## Interpretar LIMIT correctamente

Una línea `TERMINAL status=LIMIT` demuestra únicamente que se agotó el presupuesto configurado antes de otra instrucción. No demuestra que el programa no termine con un presupuesto mayor y no resuelve el problema de la parada.
