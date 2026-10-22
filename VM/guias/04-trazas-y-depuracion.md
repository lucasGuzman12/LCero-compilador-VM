# 4. Trazas y depuración

## Crear una traza

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
$vm = Join-Path $repo 'build\bin\Debug\lcero-vm.exe'
$programa = Join-Path $repo 'TestsParaVM\prueba_1.lci'
$traza = Join-Path $repo 'build\prueba-1.log'

& $vm --max-steps 100000 --trace $traza $programa
Get-Content -LiteralPath $traza
```

La traza es un dato de diagnóstico separado, no otro lenguaje de entrada. Registra estado inicial, línea fuente, índice ejecutable, `BEFORE`, `AFTER`, cambios de registro o memoria, saltos, `PRINT` y estado terminal. Activarla no debe modificar `O`, `pc`, `k` ni el código de salida.

Para un bucle use siempre un presupuesto conocido:

```powershell
& $vm --max-steps 4 --trace (Join-Path $repo 'build\limite.log') `
  (Join-Path $repo 'test\contracts\limit\17_bucle_bajo_limite.lci')
```

`LIMIT` certifica únicamente que se agotó el presupuesto de pasos observado. No es una prueba de que el programa no termine.

## Depurar en Visual Studio Community 2026

1. Abra la carpeta que contiene `CMakeLists.txt`.
2. Elija `x64-Debug` y el target `lcero_vm`.
3. Abra `src/vm/interpreter.cpp`.
4. Coloque un breakpoint dentro de `execute_instruction`.
5. Configure como argumento `TestsParaVM\prueba_1.lci` y como directorio de trabajo la raíz del repositorio.
6. Inicie la depuración.

Valores útiles para Watch:

- `state.program_counter()`: `pc`, índice de la próxima instrucción ejecutable.
- `state.executed_steps()`: `k`, cantidad de transiciones ya ejecutadas.
- `state.registers()`: `R0..R3`, cada uno opcional porque inicialmente no tiene valor.
- `state.initialized_memory()`: slots efectivamente escritos de la memoria dispersa.
- `instruction.opcode` y `instruction.source_line`: instrucción tipada y línea del `.lci`.

Para una inspección más detallada consulte [`../docs/vm/debugging.md`](../docs/vm/debugging.md) y [`../docs/vm/operational-semantics.md`](../docs/vm/operational-semantics.md).

