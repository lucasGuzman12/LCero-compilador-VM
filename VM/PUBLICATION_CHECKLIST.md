# Lista de control antes de publicar en GitHub

Esta carpeta contiene el código fuente de la máquina virtual, los 40 fixtures contractuales y los seis `.lci` recibidos como salida del compilador. No contiene `build/`, ejecutables, cachés de Visual Studio ni dependencias instaladas.

## Comprobación local

Desde la raíz del repositorio:

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -version '[18.0,19.0)' `
  -products Microsoft.VisualStudio.Product.Community -property installationPath
if ([string]::IsNullOrWhiteSpace($vsRoot)) { throw 'No se encontró Visual Studio Community 2026.' }
$env:VCPKG_ROOT = Join-Path $vsRoot 'VC\vcpkg'
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-compiler-outputs.ps1" -Configuration Debug
```

El último comando debe informar `COMPILER_OUTPUT_TOTAL=6 PASS=6 FAIL=0`.

## Revisión de privacidad

Antes de cada primer push, revise qué archivos serán versionados:

```powershell
git init
git status --short --untracked-files=all
git check-ignore -v AGENTS.md .env secrets\credencial.json
git grep -n -I -E "(password|passwd|secret|api[_-]?key|token)" -- .
```

La búsqueda puede señalar textos educativos como “tokens del lexer”; cada coincidencia debe revisarse, no borrarse automáticamente. No agregue credenciales reales, archivos `.env`, claves, certificados privados, volcados de memoria ni configuraciones de Codex/agentes.

## Publicación

Solo después de revisar `git status`:

```powershell
git add .
git status --short
git commit -m "Entrega de la maquina virtual L-Cero"
```

La creación del repositorio remoto y `git push` requieren la URL y autorización del equipo; esta preparación no realiza ninguna operación remota.

`LICENSE` no se agregó porque el equipo debe elegir explícitamente la licencia académica o de código abierto apropiada.
