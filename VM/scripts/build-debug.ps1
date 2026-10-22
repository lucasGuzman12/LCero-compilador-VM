[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Find-CMake {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vsPath = & $vswhere -version '[18.0,19.0)' -products Microsoft.VisualStudio.Product.Community -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($vsPath)) {
            $bundledCMake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path -LiteralPath $bundledCMake) {
                return $bundledCMake
            }
        }
    }

    throw 'CMake 4.2 o superior no fue encontrado. Instale las herramientas CMake para C++ desde Visual Studio Installer.'
}

if ([string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
    throw 'VCPKG_ROOT no esta definida. Debe apuntar a la raiz de una instalacion de vcpkg.'
}

$toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $toolchain)) {
    throw "No existe el toolchain esperado: $toolchain"
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build'
$cmake = Find-CMake
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'

# La configuracion selecciona explicitamente Windows x64 y MSVC. vcpkg resuelve
# cpp_int como dependencia de cabeceras, sin agregar una DLL al lenguaje virtual.
& $cmake --version
if ($LASTEXITCODE -ne 0) { throw 'No se pudo consultar la version de CMake.' }

& $cmake -S $projectRoot -B $buildDirectory -G 'Visual Studio 18 2026' -A x64 "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
if ($LASTEXITCODE -ne 0) { throw 'Fallo la configuracion CMake.' }

& $cmake --build $buildDirectory --config Debug
if ($LASTEXITCODE -ne 0) { throw 'Fallo la compilacion Debug.' }

& $ctest --test-dir $buildDirectory -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Fallo CTest para Debug.' }
