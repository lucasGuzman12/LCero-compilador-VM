# Diagnóstico de la suite contractual

La suite `scripts/test-contracts.ps1` ejecuta la VM compilada directamente sobre los 40 archivos `.lci` entregados. No invoca al compilador L-Cero, no interpreta `.lc` y no modifica `expected_cases.json` ni los `.out`.

## Ejecución normal

Desde la raíz del proyecto:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test-contracts.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\scripts\test-contracts.ps1 -Configuration Release
```

Desde otra carpeta, indique la raíz absoluta:

```powershell
powershell -ExecutionPolicy Bypass `
  -File (Join-Path (Resolve-Path -LiteralPath ".").Path 'scripts\test-contracts.ps1') `
  -ProjectRoot (Resolve-Path -LiteralPath ".").Path `
  -Configuration Debug
```

Cada fixture genera exactamente una línea `[NN/40]` con estado esperado/real, `k`, comparación de `stdout` y motivo. El resumen correcto es:

```text
CONTRACT_TOTAL=40 PASS=40 FAIL=0 HALT=15 LIMIT=3 ERROR=6 ERROR_DE_CARGA=16
SUITE_RESULT=PASS
```

La suite usa un timeout externo de 10 segundos por proceso y CTest aplica además 120 segundos al test completo. Esta cota física impide que una regresión cuelgue la automatización; no reemplaza `--max-steps` ni constituye una afirmación sobre el problema de la parada.

## Reproducir un caso particular

Use el identificador del manifiesto, sin extensión:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test-contracts.ps1 `
  -Configuration Debug -CaseId 24_salida_parcial_antes_error
```

O invoque la VM directamente con los argumentos que indica `expected_cases.json`:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" `
  ".\test\contracts\runtime_error\24_salida_parcial_antes_error.lci"
$LASTEXITCODE
```

Para el caso 17 es obligatorio conservar el presupuesto:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 4 `
  ".\test\contracts\limit\17_bucle_bajo_limite.lci"
```

Nunca ejecute ese fixture sin límite para diagnosticar la suite.

## Tabla síntoma → módulo → reproducción

| Síntoma | Módulo sospechoso | Test o breakpoint recomendado |
|---|---|---|
| Estado o exit code incorrecto | `src/vm/cli.cpp`, `src/vm/interpreter.cpp` | `-CaseId ID`; breakpoint en `print_execution_status` y `interpret` |
| `k` incorrecto | `src/vm/interpreter.cpp`, `src/vm/execution_state.cpp` | breakpoint en `record_executed_instruction`; observar `previous_steps` |
| CR `0D` o bytes `PRINT` diferentes | `src/vm/main.cpp`, `ExecutionState::output_text`, opcode `PRINT` | inspeccionar el `.stdout` preservado; breakpoint en la rama `Opcode::print` |
| `ERROR_DE_CARGA` después de ejecutar pasos | `src/vm/ir_loader.cpp`, `src/vm/ir_validator.cpp`, CLI | caso 30; breakpoint en `load_and_validate`, antes de `interpret` |
| Etiqueta o salto incorrecto | `src/vm/ir_validator.cpp`, `src/vm/interpreter.cpp` | casos 08, 23, 30 y 31; observar `ValidatedIr::labels` y `JumpTarget` |
| Registro o slot aparece inicializado | `src/vm/execution_state.cpp` | casos 20–22 y regresión de reinicialización; observar `registers_` y `memory_` |
| Inmediato grande truncado | `src/vm/instruction.cpp`, parser de RI | regresión `inmediato_100_digitos`; observar el `cpp_int` en `Immediate` |
| Caso 17 excede el tiempo | argumentos de automatización | confirmar `--max-steps 4`; no atribuir el timeout a una prueba de infinitud |
| Ruta con espacios/Unicode falla | `wmain`, `std::filesystem::path`, quoting del script | regresión `ruta_unicode_y_espacios`; observar `argv` en `main.cpp` |
| CTest mata toda la suite | timeout o proceso bloqueado | `ctest -R lcero_vm_contract_cli -V -C Debug`; revisar `FAILED_ARTIFACTS` |

Si hay una discrepancia, la carpeta indicada por `FAILED_ARTIFACTS` conserva los `.stdout` y `.stderr` capturados. La suite nunca reescribe un `.out` esperado para acomodar el resultado real.

## Breakpoints en Visual Studio Community 2026

1. Abra la carpeta CMake `LCero` y seleccione `x64-Debug`.
2. Elija el target `lcero_vm` para depurar un caso individual, o abra **Test Explorer** y filtre `lcero_vm_contract_cli` para ejecutar la suite.
3. Para carga, coloque breakpoints en `load_ir_source` y `validate_ir`.
4. Para semántica dinámica, deténgase en `interpret` o `execute_instruction`.
5. Observe `state.program_counter()`, `state.executed_steps()`, `state.registers()`, `state.initialized_memory()`, `instruction.source_line` e `instruction.opcode`.
6. Mantenga la carpeta `LCero` como directorio de trabajo si usa rutas relativas.

La sesión gráfica debe reproducirse manualmente; la verificación automática no afirma haber abierto el IDE ni observado un breakpoint.

## Mapeo de clases descriptivas

`error_class_expected` no congela los nombres de diagnóstico. La suite documenta estas agrupaciones:

- `LECTURA_NO_INICIALIZADA` acepta `REGISTRO_NO_INICIALIZADO` o `SLOT_NO_INICIALIZADO`;
- `FORMATO_INVALIDO` acepta el diagnóstico más preciso `OPERANDO_INVALIDO` del caso 36;
- `ETIQUETA_INVALIDA` acepta `ETIQUETA_RESERVADA` para el caso 38.

El resto se compara directamente. Este mapeo no altera la ISA, el archivo esperado ni la semántica de la VM.
