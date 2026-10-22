# Ejecutar programas producidos por el compilador

Esta guía cubre los seis archivos recibidos en `TestsParaVM` y cualquier programa nuevo que el compilador del grupo emita conforme a `LCERO_IR 1`. La VM nunca necesita el `.lc`, el AST ni los tokens originales: el archivo `.lci` es la interfaz formal completa entre compilador y máquina virtual.

## Probar los seis casos entregados

Desde la carpeta `LCero`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-compiler-outputs.ps1" `
  -Configuration Debug

powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-compiler-outputs.ps1" `
  -Configuration Release
```

El script localiza `TestsParaVM` dentro del repositorio publicable y también las variantes externas `..\TestsParaVM\*.lci` y `..\TestsParaVM\TestsParaVM\*.lci`. También puede indicarse una ubicación explícita:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-compiler-outputs.ps1" `
  -Configuration Debug `
  -TestsRoot (Join-Path (Resolve-Path -LiteralPath ".").Path 'TestsParaVM')
```

Por cada archivo, el runner realiza dos operaciones distintas:

1. `--check` comprueba que la cadena pertenece al lenguaje de RI: versión, gramática, tipos de operandos, rango de memoria y resolución de etiquetas. No modifica el estado operacional ni ejecuta instrucciones.
2. La ejecución crea `C=(P,pc,R,M,O,k)`, aplica transiciones hasta `HALT` y compara `O` byte a byte con el resultado esperado. El presupuesto predeterminado del runner es `100000` pasos y puede cambiarse con `-MaxSteps`.

Las capturas quedan en `build\tests-para-vm\Debug` o `Release`. Los archivos `.stdout` conservan los bytes producidos por `PRINT`; los `.stderr` contienen `LCERO_STATUS`, `LCERO_STEPS`, `PC` y categoría.

## Resultados esperados

| Caso | Salida, una línea por valor | Pasos observados Debug |
|---|---|---:|
| `prueba_1.lci` | `38`, `3`, `-3`, `26` | 79 |
| `prueba_1_optimizada.lci` | `38`, `3`, `-3`, `26` | 35 |
| `prueba_2.lci` | `100`, `400`, `600`, `700` | 76 |
| `prueba_2_optimizada.lci` | `100`, `400`, `600`, `700` | 34 |
| `prueba_3.lci` | `60`, `40`, `5`, `4` | 708 |
| `prueba_3_optimizada.lci` | `60`, `40`, `5`, `4` | 318 |

Las parejas normal/optimizada poseen distinta secuencia `P`, distinta cota `MEM` y distinto `k`, pero la misma salida observable `O`. En estos seis ejemplos, la optimización preserva el comportamiento visible y reduce los pasos. Esto es evidencia de prueba para estas entradas, no una demostración general de equivalencia semántica de todas las optimizaciones del compilador.

## Ejecutar un único programa

Primero valide el `.lci` sin ejecutarlo:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" `
  --check `
  (Resolve-Path -LiteralPath ".\TestsParaVM\prueba_1.lci").Path
$LASTEXITCODE
```

Después ejecútelo con un presupuesto explícito:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" `
  --max-steps 100000 `
  (Resolve-Path -LiteralPath ".\TestsParaVM\prueba_1.lci").Path
$LASTEXITCODE
```

En Release solo cambia la ruta del ejecutable:

```powershell
& ".\build\bin\Release\lcero-vm.exe" `
  --max-steps 100000 `
  (Resolve-Path -LiteralPath ".\TestsParaVM\prueba_1.lci").Path
```

Un exit code 0 acompañado por `LCERO_STATUS=HALT` significa que se ejecutó `HALT`. `--check` exitoso solo confirma validez estática: no garantiza terminación ni ausencia de división por cero o lecturas no inicializadas en la rama recorrida.

### Ejecutar desde otra carpeta

Puede abrir PowerShell en cualquier directorio, invocar el `.exe` mediante su ruta absoluta y conservar `TestsParaVM\prueba_1.lci` como argumento relativo:

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
$vm = Join-Path $repo 'build\bin\Debug\lcero-vm.exe'
Push-Location ([IO.Path]::GetTempPath())
try {
  & $vm --max-steps 100000 "TestsParaVM\prueba_1.lci"
} finally {
  Pop-Location
}
```

La VM prueba primero la ruta relativa al CWD. Si allí no existe, obtiene la ubicación real de `lcero-vm.exe` y asciende por sus directorios hasta encontrar la misma ruta. De este modo funciona desde Visual Studio, una terminal externa o una carpeta distinta sin guardar una ruta de usuario dentro del código. Una ruta absoluta se usa exactamente como fue recibida.

## Inspeccionar una ejecución

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
$vm = Join-Path $repo 'build\bin\Debug\lcero-vm.exe'
$programa = Join-Path $repo 'TestsParaVM\prueba_1.lci'
& $vm `
  --max-steps 100000 `
  --trace (Join-Path $repo 'build\programa.log') `
  $programa
```

La traza relaciona cada línea fuente `.lci` con el índice ejecutable 0-based, registra `pc`, `k`, opcode y diferencias de estado. No forma parte de la salida del programa: `stdout` sigue reservado exclusivamente a `PRINT`.

## Cómo agregar otra salida del compilador

1. Conserve el `.lci` emitido sin editarlo manualmente.
2. Ejecute `--check`. Si falla, lea `LINE`, `CODE` y `MESSAGE`: se trata de una incompatibilidad entre lo emitido y la gramática `LCERO_IR 1`.
3. Ejecútelo con un límite conocido. `LIMIT` solo significa que se agotó ese presupuesto; no prueba que el programa sea infinito.
4. Capture `stdout` y `stderr` por separado. Compare `stdout` por bytes para garantizar LF `0A` y evitar que la consola o PowerShell oculten un CR `0D`.
5. Si existe versión optimizada, compare la salida y el estado terminal, no necesariamente `k`, `pc`, `MEM` ni la disposición de registros: esas propiedades internas pueden variar sin cambiar la semántica observable.

Para incorporar permanentemente un caso nuevo al runner se debe registrar su nombre y salida esperada en `scripts/test-compiler-outputs.ps1`. No debe modificarse la salida esperada para ocultar una discrepancia.
