# Mini-guía para ejecutar y comprobar la VM L-Cero

Esta guía permite probar la máquina virtual desde PowerShell, mediante el ejecutable ya compilado, o desde Visual Studio Community 2026. Todos los comandos se ejecutan desde la carpeta `LCero`, donde se encuentra `CMakeLists.txt`.

## 1. Comprobación rápida con el `.exe`

El ejecutable Debug se encuentra en:

```text
build\bin\Debug\lcero-vm.exe
```

Compruebe primero que la CLI responde:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" --version
& ".\build\bin\Debug\lcero-vm.exe" --help
```

La versión esperada para esta etapa es:

```text
lcero-vm 0.7.0 (etapa 07)
```

### Validar sin ejecutar

`--check` comprueba el formato `LCERO_IR 1`, operandos, etiquetas y saltos, pero no ejecuta instrucciones:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" --check ".\test\contracts\valid\01_hola_registros.lci"
$LASTEXITCODE
```

Resultado esperado en `stderr` y código de salida:

```text
LCERO_CHECK=VALID
0
```

Esto solo demuestra que la RI está bien formada. No demuestra todavía que el programa alcance `HALT`.

### Ejecutar un programa mínimo

```powershell
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\valid\01_hola_registros.lci"
$LASTEXITCODE
```

Resultado esperado:

```text
5
LCERO_STATUS=HALT LCERO_STEPS=3 PC=2 CATEGORY=TERMINACION
0
```

El `5` pertenece a `stdout` y fue producido por `PRINT`. La línea `LCERO_STATUS` pertenece a `stderr`: describe que la máquina terminó en `HALT`, ejecutó tres instrucciones y dejó `pc` sobre el `HALT`.

### Probar cálculo, memoria, condición y bucle

El contador descendente ejercita registros, memoria, comparaciones, `JZ`, `JMP`, varios `PRINT` y `HALT`:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\valid\08_bucle_descendente.lci"
$LASTEXITCODE
```

Resultado esperado:

```text
3
2
1
LCERO_STATUS=HALT LCERO_STEPS=40 PC=13 CATEGORY=TERMINACION
0
```

Las directivas `LABEL`, el encabezado y `MEM` no cuentan como pasos; únicamente las instrucciones ejecutables incrementan `LCERO_STEPS`.

### Ejecutar con una traza depurable

```powershell
& ".\build\bin\Debug\lcero-vm.exe" --trace ".\build\contador.log" ".\test\contracts\valid\08_bucle_descendente.lci"
Get-Content -LiteralPath ".\build\contador.log"
```

La salida observable sigue siendo `3`, `2`, `1`, cada valor con LF. El log se escribe aparte y contiene el estado inicial, la relación entre línea fuente e índice ejecutable, un registro `BEFORE` y `AFTER` por transición y el terminal `HALT`. Se usan diferencias: un `STORE` muestra el slot cambiado, una operación muestra su registro destino y un salto muestra el nuevo `pc`; no se copia toda la memoria en cada línea.

Para comparar automáticamente que la traza no altera `stdout`:

```powershell
$exe = (Resolve-Path ".\build\bin\Debug\lcero-vm.exe").Path
$programa = (Resolve-Path ".\test\contracts\valid\01_hola_registros.lci").Path
$argumentoSimple = '"' + $programa + '"'
$argumentosTrace = '--trace ".\build\hola.log" "' + $programa + '"'
Start-Process -FilePath $exe -ArgumentList $argumentoSimple -NoNewWindow -Wait `
    -RedirectStandardOutput ".\build\sin-trace.out" `
    -RedirectStandardError ".\build\sin-trace.err"
Start-Process -FilePath $exe -ArgumentList $argumentosTrace -NoNewWindow -Wait `
    -RedirectStandardOutput ".\build\con-trace.out" `
    -RedirectStandardError ".\build\con-trace.err"
Compare-Object ([IO.File]::ReadAllBytes(".\build\sin-trace.out")) ([IO.File]::ReadAllBytes(".\build\con-trace.out"))
```

Sin salida de `Compare-Object`, ambos vectores de bytes son iguales.

## 2. Comprobar terminaciones diferentes

### Límite de pasos

```powershell
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 4 ".\test\contracts\limit\17_bucle_bajo_limite.lci"
$LASTEXITCODE
```

Resultado esperado:

```text
LCERO_STATUS=LIMIT LCERO_STEPS=4 PC=0 CATEGORY=PRESUPUESTO
4
```

El presupuesto se comprueba antes de la siguiente instrucción. `--max-steps 0` también es válido. Use `--no-step-limit` únicamente con programas que sabe que terminan, porque desactiva la protección contra bucles prolongados.

### Error dinámico

Un archivo puede ser válido para `--check` y fallar al ejecutarse. La división por cero es una propiedad dinámica:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" ".\test\contracts\runtime_error\19_division_por_cero.lci"
$LASTEXITCODE
```

Resultado esperado:

```text
LCERO_STATUS=ERROR LCERO_STEPS=2 PC=2 CATEGORY=EJECUCION LINE=5 CODE=DIVISION_POR_CERO ...
2
```

### Error de carga

```powershell
& ".\build\bin\Debug\lcero-vm.exe" --check ".\test\contracts\load_error\25_version_invalida.lci"
$LASTEXITCODE
```

Resultado esperado: `LCERO_STATUS=ERROR_DE_CARGA LCERO_STEPS=0 PC=0 CATEGORY=CARGA`, código `VERSION_INVALIDA` y exit code 3. No se ejecuta ninguna instrucción.

## 3. Significado de los códigos de salida

| Código | Significado |
|---:|---|
| 0 | El programa ejecutó `HALT`, o `--check` validó correctamente |
| 2 | `ERROR` durante la ejecución |
| 3 | `ERROR_DE_CARGA`: la RI es inválida |
| 4 | `LIMIT`: se agotó el presupuesto de pasos |
| 5 | Argumentos, ruta, archivo o recurso físico inválido |

En PowerShell, el código de la última ejecución se consulta mediante `$LASTEXITCODE`.

## 4. Compilar y probar desde Visual Studio Community 2026

1. Abra Visual Studio Community 2026.
2. Seleccione **Abrir > Carpeta** y elija la carpeta `LCero` que contiene `CMakeLists.txt`.
3. Asegúrese de que `VCPKG_ROOT` apunte a la instalación de vcpkg. Puede localizar la copia integrada sin codificar una ruta del equipo:

   ```powershell
   $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
   $vsRoot = & $vswhere -latest -version '[18.0,19.0)' `
     -products Microsoft.VisualStudio.Product.Community -property installationPath
   if ([string]::IsNullOrWhiteSpace($vsRoot)) { throw 'No se encontró Visual Studio Community 2026.' }
   $env:VCPKG_ROOT = Join-Path $vsRoot 'VC\vcpkg'
   ```

   Si cambia una variable de entorno después de abrir Visual Studio, cierre y vuelva a abrir el IDE para que la reciba.

4. Espere a que Visual Studio termine la configuración CMake.
5. Seleccione el preset `vs2026-x64` y la configuración `x64-Debug`.
6. Use **Compilar > Compilar todo**. El ejecutable debe aparecer en `build\bin\Debug\lcero-vm.exe`.
7. Seleccione `lcero_vm` como proyecto de inicio y use el botón de ejecución sin agregar argumentos. Se abrirá y ejecutará `Source/programa.lci` dentro de `VM`; el ejemplo incluido imprime `5`.
8. Para que la consola permanezca visible al terminar, use **Iniciar sin depurar** (Ctrl+F5). Para ejecutar otro archivo, reemplace `Source/programa.lci` por el `.lci` emitido por el compilador o siga usando los argumentos habituales de la CLI.

La configuración comprobada produjo `build\LCeroVM.slnx`. Es válido abrir esa solución generada, seleccionar plataforma `x64` y configuración Debug o Release. Otra versión del generador podría producir `.sln`. No agregue ninguno de esos artefactos al control de versiones: todo `build\` es regenerable y está excluido mediante `.gitignore`.

Para depurar una transición:

1. Abra `src/vm/interpreter.cpp`.
2. Coloque un breakpoint dentro de `execute_instruction`, por ejemplo en la rama `Opcode::constant`.
3. Seleccione `lcero_vm` como elemento de inicio.
4. Sin argumentos se usa `Source/programa.lci`. Para depurar otro archivo, abra **Depurar > Configuración de depuración e inicio** para ese target y agregue como argumento su ruta `.lci`, por ejemplo `test\contracts\valid\01_hola_registros.lci`.
5. Inicie la depuración. Podrá inspeccionar `pc`, `k`, registros, memoria dispersa y el opcode actual sin involucrar código fuente `.lc` ni estructuras del compilador.

Los nombres exactos de algunos menús pueden variar ligeramente según el idioma de Visual Studio. Si el selector de inicio no permite añadir argumentos, use la terminal integrada para ejecutar el programa y seleccione `lcero_vm_interpreter_tests` para depurar las transiciones automatizadas.

## 5. Ejecutar todas las pruebas automatizadas

Desde PowerShell:

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -version '[18.0,19.0)' `
  -products Microsoft.VisualStudio.Product.Community -property installationPath
$env:VCPKG_ROOT = Join-Path $vsRoot 'VC\vcpkg'
& ".\scripts\build-debug.ps1"
& ".\scripts\smoke.ps1"
```

`build-debug.ps1` configura CMake, compila Debug y ejecuta CTest. El resultado correcto actual es `6/6 tests passed`; la prueba contractual recorre los 40 fixtures mediante la CLI real y compara los bytes de `stdout`, y la prueba de trazas cubre equivalencia observable, saltos, `LIMIT`, `ERROR` y ruta inaccesible. `smoke.ps1` comprueba además la creación de un log real.

La matriz contractual también puede ejecutarse de forma independiente en ambas configuraciones:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test-contracts.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\scripts\test-contracts.ps1 -Configuration Release
```

Para investigar un solo fixture agregue `-CaseId 30_etiqueta_inexistente`. La guía de síntomas, artefactos fallidos y breakpoints se encuentra en [`troubleshooting.md`](troubleshooting.md).

## 6. Comprobar que `PRINT` usa LF exacto

Para evitar que PowerShell transforme la salida, rediríjala directamente mediante `Start-Process`:

```powershell
$exe = (Resolve-Path ".\build\bin\Debug\lcero-vm.exe").Path
$programa = (Resolve-Path ".\test\contracts\valid\08_bucle_descendente.lci").Path
$salida = Join-Path (Resolve-Path ".\build").Path "contador.actual"
$estado = Join-Path (Resolve-Path ".\build").Path "contador.stderr"
$argumento = '"' + $programa + '"'

$proceso = Start-Process -FilePath $exe -ArgumentList $argumento `
    -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $salida `
    -RedirectStandardError $estado

[BitConverter]::ToString([IO.File]::ReadAllBytes($salida))
$proceso.ExitCode
Get-Content -LiteralPath $estado
```

La secuencia esperada es:

```text
33-0A-32-0A-31-0A
```

Corresponde a `3\n2\n1\n`. No debe aparecer el byte `0D`, que representaría un retorno de carro CR.

## 7. Diagnóstico rápido

- Si no existe `build\bin\Debug\lcero-vm.exe`, compile primero con Visual Studio o `scripts\build-debug.ps1`.
- Si CMake no encuentra vcpkg, revise `VCPKG_ROOT` y que exista `scripts\buildsystems\vcpkg.cmake` bajo esa ruta.
- Si aparece `ERROR_DE_CARGA`, use primero `--check` y lea `LINE`, `CODE` y `MESSAGE`.
- Si aparece `ERROR`, el archivo pasó la validación estática pero falló una transición ejecutada.
- Si aparece `LIMIT`, aumente `--max-steps` solo cuando sea razonable; alcanzar el límite no demuestra matemáticamente que el programa sea infinito.
- Si aparece `LCERO_CLI_ERROR=TRAZA`, compruebe que el directorio padre exista, que la ruta sea escribible y que el log no tenga extensión `.lci`.
- Si `stdout` se mezcla visualmente con `stderr` en la consola, redirija ambos canales por separado como en la sección 6.

La configuración detallada de breakpoints y Watch está en [`debugging.md`](debugging.md), y las reglas formales de transición en [`operational-semantics.md`](operational-semantics.md).
