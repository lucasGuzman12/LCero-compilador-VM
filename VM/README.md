# Máquina virtual L-Cero

Máquina virtual independiente para `LCERO_IR 1`, en C++17, Windows x64 y Visual Studio Community 2026. Las etapas 00 a 07 implementan la CLI, el modelo operacional, la carga/validación estática, la ejecución completa de la ISA, errores robustos, trazas depurables, una suite contractual repetible y una entrega integrable con el compilador del grupo.

## Frontera académica

La VM consumirá exclusivamente archivos `.lci`. El compilador, su lexer, sus tokens, su AST y el lenguaje fuente `.lc` quedan fuera de este proyecto. Esta separación preserva a `LCERO_IR 1` como interfaz formal entre dos componentes distintos.

La ISA congelada contiene `CONST`, `LOAD`, `STORE`, `MOV`, `ADD`, `SUB`, `MUL`, `DIV`, los seis `CMP_*`, `JMP`, `JZ`, `PRINT` y `HALT`; `LABEL` es una directiva y nunca consume un paso. La implementación no agrega opcodes ni afirma completitud de Turing.

## Requisitos

- Windows x64.
- Visual Studio Community 2026 con **Desarrollo de escritorio con C++**, MSVC x64, Windows SDK y las herramientas CMake para C++.
- CMake 4.2 o superior con el generador `Visual Studio 18 2026`.
- vcpkg en modo manifiesto y `VCPKG_ROOT` apuntando a su raíz.

Visual Studio puede incluir CMake y vcpkg. Si `cmake` no está en `PATH`, se puede abrir *Developer PowerShell for VS 2026*. Para localizar el vcpkg integrado sin depender del nombre de usuario ni de una carpeta fija:

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -version '[18.0,19.0)' `
  -products Microsoft.VisualStudio.Product.Community -property installationPath
if ([string]::IsNullOrWhiteSpace($vsRoot)) { throw 'No se encontró Visual Studio Community 2026.' }
$env:VCPKG_ROOT = Join-Path $vsRoot 'VC\vcpkg'
if (-not (Test-Path (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'))) {
  throw 'La instalación detectada no contiene el toolchain de vcpkg.'
}
```

Para conservar la variable en futuras terminales:

```powershell
[Environment]::SetEnvironmentVariable('VCPKG_ROOT', $env:VCPKG_ROOT, 'User')
```

Reabra la terminal después de persistirla. Si usa un vcpkg independiente, asigne a `VCPKG_ROOT` la ruta resuelta de esa instalación. Los archivos CMake del proyecto no codifican rutas de usuarios ni de equipos concretos.

## Configuración, compilación y prueba

Desde esta carpeta:

```powershell
cmake --version
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
& ".\build\bin\Debug\lcero-vm.exe" --help
& ".\build\bin\Debug\lcero-vm.exe" --version
& ".\build\bin\Debug\lcero-vm.exe" --check ".\test\contracts\valid\01_hola_registros.lci"
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\valid\08_bucle_descendente.lci"
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 4 ".\test\contracts\limit\17_bucle_bajo_limite.lci"
& ".\build\bin\Debug\lcero-vm.exe" --trace ".\build\contador.log" ".\test\contracts\valid\08_bucle_descendente.lci"
powershell -ExecutionPolicy Bypass -File ".\scripts\test-contracts.ps1" -Configuration Debug
powershell -ExecutionPolicy Bypass -File ".\scripts\test-contracts.ps1" -Configuration Release
```

También se puede ejecutar `./scripts/build-debug.ps1`; localiza la copia de CMake incluida en Visual Studio si no está en `PATH`, pero exige una `VCPKG_ROOT` válida. `./scripts/smoke.ps1` verifica ayuda, versión, argumentos, `--check`, `HALT`, `LIMIT` y un error dinámico.

Los ejecutables quedan en `build/bin/Debug/lcero-vm.exe` y `build/bin/Release/lcero-vm.exe`.

## Alcance actual

- `--help` y `--version`: disponibles, salida informativa por `stdout`.
- `--check archivo.lci`: valida bytes, gramática, operandos, slots y etiquetas sin ejecutar; informa por `stderr` y usa exit 0/3/5.
- Sin argumentos, opción desconocida, ruta inválida o archivo inaccesible: error de CLI por `stderr`, código 5.
- Ruta existente `.lci` sin `--check`: carga, valida y ejecuta las 18 instrucciones aprobadas. `HALT`, `ERROR` y `LIMIT` devuelven respectivamente 0, 2 y 4.
- Las rutas relativas se buscan primero en el directorio de trabajo. Si el archivo no existe allí, la VM asciende desde la ubicación real de `lcero-vm.exe`; esto permite usar `TestsParaVM\prueba_1.lci` desde otra carpeta sin fijar una ruta personal. Las rutas absolutas no se reinterpretan.
- `--max-steps N` limita antes de la siguiente instrucción; `--max-steps 0` es válido. `--no-step-limit` debe pedirse explícitamente y no puede combinarse con el límite numérico.
- `--trace ruta.log` crea un log UTF-8/LF con estado inicial, pares `BEFORE`/`AFTER`, diffs de estado y resultado terminal. La traza no altera `PRINT`, `pc`, `k` ni el código de una ejecución exitosa.
- `stdout` queda reservado a `PRINT`, en decimal y con byte LF exacto incluso bajo redirección en Windows. Estados y diagnósticos van a `stderr`.
- El núcleo permite construir instrucciones tipadas y el estado `C=(P,pc,R,M,O,k)` desde tests.
- Los registros y slots comienzan ausentes; un valor cero se conserva como valor presente.
- La memoria es dispersa y mantiene su cota lógica como `cpp_int`.
- Los errores conocidos se devuelven como diagnósticos explícitos; el loader no deja escapar fallos esperables del archivo como excepciones sin tratar.
- El loader y validador producen instrucciones tipadas; el intérprete ejecuta ese `Program` inmutable sin volver a parsear texto. Un `DIV` por cero bien formado pasa `--check` y solo falla si la transición llega a ejecutarse.
- Cada resultado de ejecución informa `LCERO_STATUS`, `LCERO_STEPS`, `PC` y `CATEGORY` en `stderr`. Un error de carga informa `k=0`, `PC=0` y no produce `PRINT`.

Las convenciones completas están en `docs/decisions.md`; el avance verificable se registra en `docs/progress.md`.

Para comenzar desde cero, consulte el índice para principiantes [`guias/README.md`](guias/README.md). Si PowerShell no reconoce CMake o necesita configurar vcpkg, siga [`GUIA_CONFIGURAR_CMAKE_Y_VCPKG.md`](GUIA_CONFIGURAR_CMAKE_Y_VCPKG.md). Para ejecutar directamente un archivo del compilador desde la terminal, consulte [`GUIA_EJECUTAR_UN_ARCHIVO_LCI.md`](GUIA_EJECUTAR_UN_ARCHIVO_LCI.md). Para una referencia técnica paso a paso mediante el `.exe`, PowerShell o Visual Studio Community 2026, consulte [`docs/vm/guia-ejecucion.md`](docs/vm/guia-ejecucion.md).

La documentación final se divide por responsabilidad:

- [`architecture.md`](docs/vm/architecture.md): flujo loader–validador–estado–intérprete y fronteras del sistema;
- [`operational-semantics.md`](docs/vm/operational-semantics.md): relación de transición sobre `C=(P,pc,R,M,O,k)`;
- [`compliance.md`](docs/vm/compliance.md): tabla opcode–semántica–pruebas y limitaciones reales;
- [`integration-guide.md`](docs/vm/integration-guide.md): contrato para el `.lci` producido por el compilador;
- [`debugging.md`](docs/vm/debugging.md) y [`troubleshooting.md`](docs/vm/troubleshooting.md): breakpoints y diagnóstico;
- [`measurement-plan.md`](docs/vm/measurement-plan.md): propuesta del Hito III, todavía sin resultados de profiling.
- [`pruebas-del-compilador.md`](docs/vm/pruebas-del-compilador.md): ejecución de los seis `.lci` recibidos y de nuevos programas emitidos por el compilador.

## Salidas reales del compilador del grupo

La carpeta `TestsParaVM` incluida en la distribución publicable —o ubicada junto al proyecto durante el desarrollo— contiene tres programas y sus tres versiones optimizadas. Para validar y ejecutar los seis, comparando `stdout` byte a byte con la expectativa entregada:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-compiler-outputs.ps1" `
  -Configuration Debug

powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-compiler-outputs.ps1" `
  -Configuration Release
```

El runner no depende del código interno del compilador: consume exactamente los `.lci` emitidos. Las instrucciones para una ruta arbitraria, trazas y diagnóstico están en la guía enlazada arriba.

## Suite contractual reproducible

El runner PowerShell lee `test/contracts/expected_cases.json` con `ConvertFrom-Json`, ejecuta el `.exe` real una vez por fixture y compara estado, exit code, `k` cuando está fijado y `stdout` byte a byte:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test-contracts.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\scripts\test-contracts.ps1 -Configuration Release
```

La salida correcta contiene 40 líneas de caso y termina con:

```text
CONTRACT_TOTAL=40 PASS=40 FAIL=0 HALT=15 LIMIT=3 ERROR=6 ERROR_DE_CARGA=16
SUITE_RESULT=PASS
```

Para reproducir solamente un caso:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test-contracts.ps1 `
  -Configuration Debug -CaseId 24_salida_parcial_antes_error
```

Además de los 40 fixtures, el runner comprueba rutas Unicode/con espacios, un inmediato de 100 dígitos, presupuestos 0/1/3 y reinicialización entre archivos. Cada proceso tiene timeout externo y CTest limita la suite completa a 120 segundos. Consulte la tabla de síntomas en [`docs/vm/troubleshooting.md`](docs/vm/troubleshooting.md).

## Trazas reproducibles

```powershell
# HALT: el stdout sigue siendo exactamente 5 seguido por LF
& ".\build\bin\Debug\lcero-vm.exe" --trace ".\build\hola.log" ".\test\contracts\valid\01_hola_registros.lci"

# Saltos JZ/JMP, memoria y HALT con k=40
& ".\build\bin\Debug\lcero-vm.exe" --trace ".\build\bucle.log" ".\test\contracts\valid\08_bucle_descendente.lci"

# Este bucle se ejecuta únicamente bajo el presupuesto explícito
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 4 --trace ".\build\limite.log" ".\test\contracts\limit\17_bucle_bajo_limite.lci"

# Conserva 8+LF en stdout y las líneas previas del log antes del ERROR
& ".\build\bin\Debug\lcero-vm.exe" --trace ".\build\error.log" ".\test\contracts\runtime_error\24_salida_parcial_antes_error.lci"

# Fallo explícito de apertura: el directorio padre no existe, no se ejecuta PRINT
& ".\build\bin\Debug\lcero-vm.exe" --trace ".\build\no-existe\traza.log" ".\test\contracts\valid\01_hola_registros.lci"
$LASTEXITCODE  # 5
```

Una traza es información diagnóstica, no un segundo lenguaje ni una entrada aceptada por la VM. `LIMIT` registra el agotamiento del presupuesto observado, no una prueba de infinitud.

## Reproducción de errores y presupuestos

```powershell
# HALT exactamente al consumir N=3
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 3 ".\test\contracts\limit\15_halt_exacto_limite.lci"

# LIMIT después de CONST y PRINT; conserva la salida parcial
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 2 ".\test\contracts\limit\16_limite_antes_halt.lci"

# Bucle protegido: este caso siempre se ejecuta con límite explícito
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 4 ".\test\contracts\limit\17_bucle_bajo_limite.lci"

# LIMIT antes de la primera instrucción
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 0 ".\test\contracts\limit\18_presupuesto_cero.lci"

# Seis errores dinámicos distintos; todos deben devolver exit 2
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\runtime_error\19_division_por_cero.lci"
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\runtime_error\20_slot_sin_inicializar.lci"
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\runtime_error\21_print_registro_no_inicializado.lci"
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\runtime_error\22_store_registro_no_inicializado.lci"
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\runtime_error\23_caida_sin_halt.lci"
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\runtime_error\24_salida_parcial_antes_error.lci"

# Error estático: no ejecuta pasos ni produce PRINT
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\load_error\30_etiqueta_inexistente.lci"

# Errores de CLI; devuelven exit 5, no HALT
& ".\build\bin\Debug\lcero-vm.exe" --max-steps -1 ".\test\contracts\valid\14_halt_solo.lci"
& ".\build\bin\Debug\lcero-vm.exe" --max-steps abc ".\test\contracts\valid\14_halt_solo.lci"
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 1 --no-step-limit ".\test\contracts\valid\14_halt_solo.lci"
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\no_existe.lci"
```

Tras cada invocación, `$LASTEXITCODE` muestra el código del proceso. Alcanzar `LIMIT` solo demuestra que se agotó el presupuesto observado; no prueba que el programa sea infinito.

## Depuración con Visual Studio Community 2026

1. Abra Visual Studio Community 2026 y elija **Abrir > Carpeta**; seleccione la carpeta `LCero` que contiene `CMakeLists.txt`.
2. Compruebe que `VCPKG_ROOT` esté disponible en el entorno con el que inició Visual Studio.
3. En la barra de configuración CMake seleccione el preset de Visual Studio 2026 y la configuración `x64-Debug`.
4. En el Explorador de soluciones localice `tests/vm/interpreter_tests.cpp` y coloque un breakpoint, por ejemplo, dentro de `test_all_valid_programs` antes de llamar a `interpret`.
5. Seleccione `lcero_vm_interpreter_tests.exe` como elemento de inicio, configure como argumento la ruta absoluta a `test/contracts` y use **Depurar > Iniciar depuración**.

Como alternativa, la configuración comprobada genera `build/LCeroVM.slnx`. Puede abrir esa solución de Visual Studio, elegir `Debug` o `Release` y plataforma `x64`, y seleccionar `lcero_vm` como proyecto de inicio. Todo `build/` es regenerable y está excluido por `.gitignore`: no debe versionarse ni entregarse como fuente. Otra versión del generador podría usar `.sln`; aplique la misma regla al artefacto que produzca localmente.

Estos son pasos de reproducción; no se afirma haber realizado una sesión gráfica ni haber observado un breakpoint desde el IDE.

## Tres ejecuciones manuales

```powershell
# Suma simple: stdout 5+LF, HALT, k=5
& ".\build\bin\Debug\lcero-vm.exe" ".\examples\suma_simple.lci"

# Bucle finito: stdout 3+LF, 2+LF, 1+LF; HALT, k=40
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\valid\08_bucle_descendente.lci" --max-steps 100000

# Archivo bien formado que conserva stdout parcial antes de ERROR dinámico
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\runtime_error\24_salida_parcial_antes_error.lci"
```

No ejecute `17_bucle_bajo_limite.lci` sin protección: use exactamente `--max-steps 4`. Alcanzar `LIMIT` no prueba no terminación; solo certifica que la máquina consumió el presupuesto establecido.
