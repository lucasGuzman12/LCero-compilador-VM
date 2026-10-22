# 1. Instalación y compilación

## Requisitos

- Windows x64.
- Visual Studio Community 2026 con **Desarrollo de escritorio con C++**, MSVC x64, Windows SDK y herramientas CMake.
- CMake 4.2 o superior y generador `Visual Studio 18 2026`.
- vcpkg y la dependencia declarada `boost-multiprecision`.

`boost::multiprecision::cpp_int` permite representar enteros arbitrariamente grandes. Sustituirlo por `int`, `long long` o coma flotante alteraría la semántica matemática de `LCERO_IR 1`.

## 1. Abrir PowerShell en el repositorio

Descargue o clone el proyecto, abra PowerShell en la carpeta que contiene `CMakeLists.txt` y calcule su ubicación real:

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
```

## 2. Localizar vcpkg sin una ruta fija

Si usa la copia incluida en Visual Studio:

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'No se encontró vswhere.exe.' }

$vsRoot = & $vswhere -latest -version '[18.0,19.0)' `
  -products Microsoft.VisualStudio.Product.Community -property installationPath
if ([string]::IsNullOrWhiteSpace($vsRoot)) { throw 'No se encontró Visual Studio Community 2026.' }

$env:VCPKG_ROOT = Join-Path $vsRoot 'VC\vcpkg'
$toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $toolchain)) { throw 'No se encontró el toolchain de vcpkg.' }
```

Si instaló vcpkg por separado, defina `VCPKG_ROOT` con la ruta de esa instalación. Para persistir el valor que ya comprobó:

```powershell
[Environment]::SetEnvironmentVariable('VCPKG_ROOT', $env:VCPKG_ROOT, 'User')
```

Después deberá abrir una terminal nueva.

## 3. Configurar y compilar

```powershell
cmake --version
cmake -S $repo -B (Join-Path $repo 'build') `
  -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$toolchain"
cmake --build (Join-Path $repo 'build') --config Debug
cmake --build (Join-Path $repo 'build') --config Release
```

Ejecutables generados:

- `build\bin\Debug\lcero-vm.exe`
- `build\bin\Release\lcero-vm.exe`

`build` es una carpeta local regenerable. CMake guarda allí rutas absolutas de la máquina que compiló, por lo que está excluida de Git y no debe copiarse como código fuente.

## Alternativa automatizada

Con `VCPKG_ROOT` ya definida:

```powershell
& (Join-Path $repo 'scripts\build-debug.ps1')
```

El script calcula la raíz a partir de su propia ubicación (`$PSScriptRoot`), configura, compila Debug y ejecuta CTest.

## Desde Visual Studio

1. Abra Visual Studio Community 2026.
2. Seleccione **Abrir > Carpeta** y elija la carpeta que contiene `CMakeLists.txt`.
3. Seleccione el preset `vs2026-x64` y luego `x64-Debug` o `x64-Release`.
4. Use **Compilar > Compilar todo**.

Los nombres exactos de los menús pueden variar con el idioma del IDE. Estos pasos no requieren modificar rutas dentro del proyecto.

