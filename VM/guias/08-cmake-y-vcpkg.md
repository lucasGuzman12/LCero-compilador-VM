# 8. Configurar CMake y vcpkg

Esta guía permite preparar el proyecto desde una PowerShell normal aunque el comando `cmake` no esté disponible en `PATH`. Una explicación todavía más extensa, incluida la instalación autónoma de vcpkg, se encuentra en [`../GUIA_CONFIGURAR_CMAKE_Y_VCPKG.md`](../GUIA_CONFIGURAR_CMAKE_Y_VCPKG.md).

## Antes de empezar

Visual Studio Community 2026 debe tener instalados:

- Desarrollo de escritorio con C++;
- MSVC para x64;
- Windows SDK;
- herramientas CMake para C++;
- vcpkg.

El proyecto usa C++17 y `boost::multiprecision::cpp_int`. Este tipo representa enteros arbitrariamente grandes sin convertirlos a 64 bits ni a coma flotante, como exige el modelo matemático de la VM.

## Por qué aparecen variables nulas

Las variables `$repo`, `$cmake` y `$toolchain` pertenecen solamente a la PowerShell en la que fueron creadas. Una terminal nueva empieza sin ellas. Por eso no se debe copiar directamente el último comando de compilación: primero hay que ejecutar todas las asignaciones.

## Preparación completa

Abra PowerShell en la carpeta que contiene `CMakeLists.txt` y pegue el bloque completo:

```powershell
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path -LiteralPath ".").Path
if (-not (Test-Path -LiteralPath (Join-Path $repo 'CMakeLists.txt'))) {
  throw 'La terminal no está en la raíz del repositorio.'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} `
  'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
  throw 'No se encontró vswhere.exe.'
}

$vsRoot = & $vswhere -latest -version '[18.0,19.0)' `
  -products Microsoft.VisualStudio.Product.Community `
  -requires Microsoft.VisualStudio.Component.VC.CMake.Project `
  -property installationPath
if ([string]::IsNullOrWhiteSpace($vsRoot)) {
  throw 'No se encontró Visual Studio Community 2026 con CMake para C++.'
}

$cmake = Join-Path $vsRoot `
  'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vsRoot `
  'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$env:VCPKG_ROOT = Join-Path $vsRoot 'VC\vcpkg'
$vcpkg = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
$toolchain = Join-Path $env:VCPKG_ROOT `
  'scripts\buildsystems\vcpkg.cmake'
$build = Join-Path $repo 'build'

foreach ($archivo in @($cmake, $ctest, $vcpkg, $toolchain)) {
  if (-not (Test-Path -LiteralPath $archivo)) {
    throw "No existe el componente requerido: $archivo"
  }
}

& $cmake --version
& $vcpkg version
```

El operador `&` ejecuta el archivo cuya ruta está guardada en una variable. Se usa `& $cmake` en lugar de `cmake` porque una PowerShell común puede no tener CMake en `PATH`.

## Cómo genera vcpkg las dependencias

El repositorio ya contiene `vcpkg.json`, cuyo único paquete directo es `boost-multiprecision`. No se crea otro manifiesto y no se instala Boost manualmente.

Al configurar CMake con el toolchain:

1. CMake carga `vcpkg.cmake`.
2. vcpkg encuentra `vcpkg.json` en la raíz.
3. Resuelve la versión mediante `builtin-baseline`.
4. Descarga o reutiliza los paquetes disponibles en caché.
5. Crea la instalación local `build\vcpkg_installed`.
6. CMake recibe las rutas de inclusión necesarias para compilar `cpp_int`.

Este es el modo manifiesto de vcpkg. Mantiene las dependencias asociadas al proyecto, en vez de depender de una instalación global de Boost.

## Configurar, compilar y probar Debug

Continúe en la misma terminal:

```powershell
& $cmake -S $repo -B $build `
  -G 'Visual Studio 18 2026' -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
if ($LASTEXITCODE -ne 0) { throw 'Falló la configuración CMake.' }

& $cmake --build $build --config Debug
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación Debug.' }

& $ctest --test-dir $build -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Falló CTest Debug.' }
```

La primera configuración puede tardar mientras vcpkg restaura los paquetes. No interrumpa el proceso solamente porque permanezca unos minutos en `Running vcpkg install`.

## Compilar y probar Release

```powershell
& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación Release.' }

& $ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Falló CTest Release.' }
```

Los ejecutables se generan en:

- `build\bin\Debug\lcero-vm.exe`;
- `build\bin\Release\lcero-vm.exe`.

## Método automatizado

Después de definir `$env:VCPKG_ROOT`, puede reemplazar la configuración, compilación y CTest Debug por:

```powershell
& ".\scripts\build-debug.ps1"
```

El script encuentra el CMake incluido en Visual Studio aunque no esté en `PATH`.

## Variables persistentes y temporales

Para conservar `VCPKG_ROOT` para terminales futuras:

```powershell
[Environment]::SetEnvironmentVariable(
  'VCPKG_ROOT',
  $env:VCPKG_ROOT,
  'User'
)
```

Para poder escribir `cmake` directamente solo durante la terminal actual:

```powershell
$cmakeDirectory = Split-Path -Parent $cmake
$env:Path = "$cmakeDirectory;$env:Path"
cmake --version
```

No es necesario persistir esta modificación del `PATH`: también puede continuar usando `& $cmake`.

## Qué no debe subirse a GitHub

`build` contiene `CMakeCache.txt`, dependencias, ejecutables y rutas absolutas del equipo que compiló. Es regenerable y ya está excluido mediante `.gitignore`. El repositorio debe conservar `vcpkg.json`, `CMakeLists.txt` y `CMakePresets.json`, pero no `build\vcpkg_installed`.

## Referencias

- [Modo manifiesto de vcpkg](https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode)
- [vcpkg con CMake y Visual Studio](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started-vs)

