# Configuración completa de CMake y vcpkg en Windows

Esta guía soluciona los errores `cmake no se reconoce`, `Join-Path ... Path es nulo` y `CMAKE_TOOLCHAIN_FILE` vacío. Los comandos están pensados para PowerShell y calculan las rutas del equipo donde se descargó el proyecto.

## Qué significan los errores observados

### `cmake` no se reconoce

Visual Studio puede tener `cmake.exe` instalado sin agregarlo al `PATH` de una PowerShell normal. Esto no significa que CMake esté ausente. Se puede localizar con `vswhere.exe` y ejecutar mediante su ruta real.

### `Join-Path $repo 'build'` recibe un valor nulo

La variable `$repo` solo existe después de asignarla en la terminal actual. Al abrir una PowerShell nueva, todas las variables creadas en una sesión anterior desaparecen.

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
```

Este comando debe ejecutarse estando en la carpeta que contiene `CMakeLists.txt`.

### `$toolchain` está vacío

La variable debe construirse después de definir `VCPKG_ROOT`:

```powershell
$toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
```

Copiar solamente el comando final de CMake, sin ejecutar las asignaciones anteriores en la misma terminal, produce exactamente estos errores.

## Conceptos: qué se instala y qué se genera

Hay tres elementos diferentes:

1. **vcpkg**: el gestor de paquetes y sus scripts.
2. **Toolchain de vcpkg**: `scripts\buildsystems\vcpkg.cmake`, que conecta vcpkg con CMake.
3. **Dependencias del proyecto**: se declaran en `vcpkg.json` y se instalan para esta compilación.

Este repositorio ya contiene el manifiesto:

```json
{
  "dependencies": [
    "boost-multiprecision"
  ]
}
```

Al configurar CMake con el toolchain, vcpkg lee automáticamente `vcpkg.json`, resuelve la versión fijada por `builtin-baseline` y crea `build\vcpkg_installed`. No se debe ejecutar `vcpkg install boost-multiprecision` en modo clásico ni editar los archivos descargados.

`boost::multiprecision::cpp_int` conserva los enteros matemáticos arbitrariamente grandes de la VM. Reemplazarlo por `int`, `long long` o coma flotante cambiaría la semántica de `LCERO_IR 1`.

## Método recomendado para este equipo: vcpkg incluido en Visual Studio

Abra una PowerShell nueva en la raíz del repositorio y pegue el bloque completo, no líneas aisladas:

```powershell
$ErrorActionPreference = 'Stop'

# 1. La terminal debe estar en la carpeta que contiene CMakeLists.txt.
$repo = (Resolve-Path -LiteralPath ".").Path
if (-not (Test-Path -LiteralPath (Join-Path $repo 'CMakeLists.txt'))) {
  throw 'Abra PowerShell en la raíz del repositorio.'
}

# 2. vswhere descubre Visual Studio sin suponer unidad o carpeta.
$vswhere = Join-Path ${env:ProgramFiles(x86)} `
  'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
  throw 'No se encontró vswhere.exe. Revise la instalación de Visual Studio.'
}

$vsRoot = & $vswhere -latest -version '[18.0,19.0)' `
  -products Microsoft.VisualStudio.Product.Community `
  -requires Microsoft.VisualStudio.Component.VC.CMake.Project `
  -property installationPath
if ([string]::IsNullOrWhiteSpace($vsRoot)) {
  throw 'No se encontró Visual Studio Community 2026 con CMake para C++.'
}

# 3. Construcción de rutas para esta instalación concreta.
$cmake = Join-Path $vsRoot `
  'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vsRoot `
  'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$env:VCPKG_ROOT = Join-Path $vsRoot 'VC\vcpkg'
$vcpkg = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
$toolchain = Join-Path $env:VCPKG_ROOT `
  'scripts\buildsystems\vcpkg.cmake'
$build = Join-Path $repo 'build'

# 4. Validación temprana: detiene el proceso con una causa clara.
foreach ($archivo in @($cmake, $ctest, $vcpkg, $toolchain)) {
  if (-not (Test-Path -LiteralPath $archivo)) {
    throw "No existe el componente requerido: $archivo"
  }
}

# 5. Mostrar las herramientas que realmente se usarán.
& $cmake --version
& $vcpkg version

# 6. Configuración. Aquí vcpkg lee vcpkg.json e instala Boost.
& $cmake -S $repo -B $build `
  -G 'Visual Studio 18 2026' -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
if ($LASTEXITCODE -ne 0) { throw 'Falló la configuración CMake.' }

# 7. Compilación y pruebas Debug.
& $cmake --build $build --config Debug
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación Debug.' }

& $ctest --test-dir $build -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Falló CTest Debug.' }
```

La primera configuración puede tardar porque vcpkg recupera e instala las dependencias. Las ejecuciones posteriores reutilizan `build\vcpkg_installed` mientras esa carpeta exista.

## Método corto usando el script del repositorio

Después de ejecutar las secciones 1 a 4 del bloque anterior, basta con:

```powershell
& ".\scripts\build-debug.ps1"
```

El script localiza CMake aunque no esté en `PATH`, configura el proyecto, compila Debug y ejecuta CTest. Todavía necesita que `VCPKG_ROOT` esté definida porque esa variable identifica explícitamente la instalación de vcpkg.

## Habilitar `cmake` solo en la terminal actual

No es obligatorio modificar el `PATH`: `& $cmake` es suficiente. Si se desea escribir `cmake` directamente durante esta sesión:

```powershell
$cmakeDirectory = Split-Path -Parent $cmake
$env:Path = "$cmakeDirectory;$env:Path"
cmake --version
```

El cambio desaparece al cerrar la terminal y no altera permanentemente el equipo.

## Conservar `VCPKG_ROOT` para terminales futuras

Después de comprobar que `$env:VCPKG_ROOT` es correcto:

```powershell
[Environment]::SetEnvironmentVariable(
  'VCPKG_ROOT',
  $env:VCPKG_ROOT,
  'User'
)
```

La terminal actual ya tiene el valor. Para que una terminal o Visual Studio abiertos anteriormente reciban el valor persistido, ciérrelos y ábralos de nuevo.

## Compilar Release

Con las variables todavía disponibles en la misma PowerShell:

```powershell
& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación Release.' }

& $ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Falló CTest Release.' }
```

Los ejecutables quedan en:

```text
build\bin\Debug\lcero-vm.exe
build\bin\Release\lcero-vm.exe
```

## Ejecutar la prueba solicitada

```powershell
& (Join-Path $build 'bin\Debug\lcero-vm.exe') `
  --check `
  (Join-Path $repo 'TestsParaVM\prueba_3_optimizada.lci')

& (Join-Path $build 'bin\Debug\lcero-vm.exe') `
  --max-steps 100000 `
  (Join-Path $repo 'TestsParaVM\prueba_3_optimizada.lci')
```

La ejecución correcta produce `60`, `40`, `5` y `4`, cada uno terminado en LF, y finaliza con `LCERO_STATUS=HALT`, `LCERO_STEPS=318` y exit code 0.

## Alternativa: presets de CMake

`CMakePresets.json` ya usa `$env{VCPKG_ROOT}`. Después de definir esa variable:

```powershell
& $cmake --preset vs2026-x64
& $cmake --build --preset debug
& $ctest --preset debug
```

Estos comandos deben ejecutarse desde la raíz del repositorio.

## Si otro equipo no incluye vcpkg

Solo use esta alternativa cuando no exista el vcpkg integrado. Requiere Git y conexión a Internet:

```powershell
$toolsRoot = Join-Path $env:LOCALAPPDATA 'LCeroTools'
New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null

$standaloneVcpkg = Join-Path $toolsRoot 'vcpkg'
git clone https://github.com/microsoft/vcpkg.git $standaloneVcpkg
& (Join-Path $standaloneVcpkg 'bootstrap-vcpkg.bat') -disableMetrics

$env:VCPKG_ROOT = (Resolve-Path -LiteralPath $standaloneVcpkg).Path
$vcpkg = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
$toolchain = Join-Path $env:VCPKG_ROOT `
  'scripts\buildsystems\vcpkg.cmake'

& $vcpkg version
```

Después continúe desde la configuración CMake del método recomendado. No clone vcpkg dentro de `src`, `include` ni `TestsParaVM`; es una herramienta externa y no forma parte del lenguaje de la VM.

## Archivos generados

- `build\CMakeCache.txt`: configuración local, incluidas rutas absolutas de este equipo.
- `build\vcpkg_installed`: dependencias del manifiesto para el triplet seleccionado.
- `build\bin\Debug` y `build\bin\Release`: ejecutables.
- otros archivos de MSBuild, CTest y depuración.

Todo `build` es regenerable y está ignorado por Git. No debe subirse al repositorio porque sus rutas pertenecen al equipo que realizó la configuración.

## Diagnóstico rápido

```powershell
$repo
$vsRoot
$cmake
$env:VCPKG_ROOT
$toolchain

Test-Path -LiteralPath $repo
Test-Path -LiteralPath $cmake
Test-Path -LiteralPath $env:VCPKG_ROOT
Test-Path -LiteralPath $toolchain
```

Si una variable se imprime vacía, vuelva a ejecutar el bloque desde su inicio. Si una ruta existe pero CMake falla al descargar paquetes, se trata de conectividad o del registro de vcpkg, no de la gramática `.lci` ni del intérprete.

## Referencias oficiales

- [Modo manifiesto de vcpkg](https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode)
- [Instalar y usar paquetes con CMake y Visual Studio](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started-vs)
- [Presets de CMake en Visual Studio](https://learn.microsoft.com/en-us/cpp/build/cmake-presets-vs)

