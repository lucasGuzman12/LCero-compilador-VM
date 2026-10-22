# Ejecutar un archivo `.lci` desde la terminal

Esta guía explica cómo ejecutar `prueba_3_optimizada.lci` desde una terminal abierta en la carpeta raíz de la máquina virtual, es decir, la carpeta que contiene `CMakeLists.txt`, `src`, `scripts` y `TestsParaVM`.

> El nombre correcto es `prueba_3_optimizada.lci`, no `prueba_3_optimizada.cli`. `.lci` es la extensión de la representación intermedia `LCERO_IR 1`; “CLI” significa interfaz de línea de comandos y no es un formato que la VM acepte.

## 1. Abrir la terminal en la carpeta correcta

En el Explorador de archivos, abra la carpeta del repositorio y seleccione **Abrir en Terminal**. También puede abrir la terminal integrada de Visual Studio o VS Code.

Compruebe que está en la raíz del proyecto:

```powershell
Test-Path ".\CMakeLists.txt"
Test-Path ".\TestsParaVM\prueba_3_optimizada.lci"
```

Ambos comandos deben mostrar `True`.

## 2. Compilar la VM si todavía no existe el ejecutable

Compruebe si ya está compilada:

```powershell
Test-Path ".\build\bin\Debug\lcero-vm.exe"
```

Si devuelve `False`, defina primero `VCPKG_ROOT` y ejecute:

```powershell
& ".\scripts\build-debug.ps1"
```

El script calcula la raíz del repositorio a partir de su propia ubicación, configura CMake, compila Debug y ejecuta CTest. No contiene una ruta del equipo que desarrolló la VM.

La detección completa de Visual Studio, CMake y vcpkg está explicada en [`GUIA_CONFIGURAR_CMAKE_Y_VCPKG.md`](GUIA_CONFIGURAR_CMAKE_Y_VCPKG.md). Ejecute primero esa guía si `cmake` no se reconoce o si `$repo`/`$toolchain` aparecen vacías.

## 3. Validar el archivo sin ejecutarlo

```powershell
& ".\build\bin\Debug\lcero-vm.exe" `
  --check `
  ".\TestsParaVM\prueba_3_optimizada.lci"
$LASTEXITCODE
```

El resultado esperado es:

```text
LCERO_CHECK=VALID
0
```

`--check` comprueba que el archivo pertenece al formato `LCERO_IR 1`, que sus operandos son válidos y que sus etiquetas pueden resolverse. No ejecuta instrucciones, no incrementa `k` y no produce valores mediante `PRINT`.

## 4. Ejecutar el programa

```powershell
& ".\build\bin\Debug\lcero-vm.exe" `
  --max-steps 100000 `
  ".\TestsParaVM\prueba_3_optimizada.lci"
$LASTEXITCODE
```

La salida esperada de `PRINT` es:

```text
60
40
5
4
```

El diagnóstico terminal esperado contiene:

```text
LCERO_STATUS=HALT LCERO_STEPS=318 PC=57 CATEGORY=TERMINACION
```

El código final debe ser `0`. Los cuatro números pertenecen a `stdout`; la línea `LCERO_STATUS` se escribe en `stderr`. Cada `PRINT` termina con LF, como exige el contrato.

## 5. Por qué ahora funciona con distintas ubicaciones

Cuando la VM recibe una ruta relativa, aplica este orden:

1. Busca el archivo respecto del directorio actual de la terminal.
2. Si no existe allí, obtiene la ubicación real de `lcero-vm.exe` y busca la misma ruta mientras asciende por sus carpetas padre.
3. Si el usuario entregó una ruta absoluta, la usa exactamente y no intenta reinterpretarla.

Por eso, desde la raíz del proyecto, la forma más simple es:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" `
  ".\TestsParaVM\prueba_3_optimizada.lci"
```

También puede calcular rutas absolutas pertenecientes al equipo donde se descargó el repositorio, sin escribir el nombre de ningún usuario:

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
$vm = Join-Path $repo 'build\bin\Debug\lcero-vm.exe'
$programa = Join-Path $repo 'TestsParaVM\prueba_3_optimizada.lci'

& $vm --max-steps 100000 $programa
```

`Resolve-Path` obtiene la ubicación real de esta copia del repositorio. Así, si otra persona descarga el proyecto en otra unidad o carpeta, los comandos construyen las rutas correctas para su propio equipo.

## 6. Ejecutar cualquier otro programa compilado

Cambie solamente el valor de `$programa`:

```powershell
$programa = (Resolve-Path -LiteralPath ".\TestsParaVM\prueba_1.lci").Path
& $vm --check $programa
& $vm --max-steps 100000 $programa
```

Para un `.lci` guardado en otra carpeta, use su ruta con `Resolve-Path`:

```powershell
$programa = (Resolve-Path -LiteralPath ".\salidas-del-compilador\programa.lci").Path
& $vm --check $programa
& $vm --max-steps 100000 $programa
```

La VM nunca ejecuta el archivo fuente `.lc`, ni recibe el lexer, tokens o AST del compilador. El compilador debe producir primero un `.lci` válido que respete la ISA aprobada.

## 7. Crear una traza opcional

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
$vm = Join-Path $repo 'build\bin\Debug\lcero-vm.exe'
$programa = Join-Path $repo 'TestsParaVM\prueba_3_optimizada.lci'
$traza = Join-Path $repo 'build\prueba_3_optimizada.log'
& $vm --max-steps 100000 --trace $traza $programa
Get-Content -LiteralPath $traza
```

La traza permite observar `pc`, `k`, opcodes y cambios de registros o memoria. Se guarda separada de `stdout`, por lo que no modifica la salida observable del programa.

## Errores rápidos

- **“No se reconoce `lcero-vm.exe`”**: compile primero o compruebe `build\bin\Debug`.
- **`LCERO_CLI_ERROR=ARCHIVO`**: confirme el nombre, la extensión `.lci` y el resultado de `Test-Path`.
- **`ERROR_DE_CARGA`**: el archivo no cumple el formato `LCERO_IR 1`; ejecute primero `--check` y revise `LINE`, `CODE` y `MESSAGE`.
- **`LIMIT`**: se consumió el presupuesto indicado. No demuestra que el programa sea infinito.
- **`ERROR`**: ocurrió un fallo dinámico, como división por cero o lectura de un registro/slot no inicializado.
