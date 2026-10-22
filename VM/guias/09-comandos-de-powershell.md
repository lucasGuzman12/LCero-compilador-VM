# 9. Comandos de PowerShell para trabajar con la VM

Esta referencia reúne los comandos cotidianos para localizar el proyecto, compilarlo, ejecutar `.lci`, revisar resultados y correr pruebas. Deben introducirse en PowerShell, no dentro de un archivo `.lci`.

## Navegación y archivos

Mostrar la carpeta actual:

```powershell
Get-Location
```

Entrar en la carpeta descargada:

```powershell
Set-Location "<carpeta donde descargó el repositorio>"
```

Confirmar que es la raíz del proyecto:

```powershell
Test-Path ".\CMakeLists.txt"
Test-Path ".\vcpkg.json"
Test-Path ".\TestsParaVM\prueba_3_optimizada.lci"
```

Listar los programas recibidos del compilador:

```powershell
Get-ChildItem ".\TestsParaVM" -Filter "*.lci"
```

`prueba_3_optimizada.lci` termina en `.lci`. La extensión `.cli` no corresponde al formato de representación intermedia.

## Construir rutas portables

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
$vm = Join-Path $repo 'build\bin\Debug\lcero-vm.exe'
$programa = Join-Path $repo 'TestsParaVM\prueba_3_optimizada.lci'
```

`Resolve-Path` obtiene una ruta absoluta perteneciente al equipo actual. `Join-Path` añade componentes sin suponer una unidad, usuario o carpeta de escritorio.

Mostrar y comprobar variables:

```powershell
$repo
$vm
$programa
Test-Path -LiteralPath $vm
Test-Path -LiteralPath $programa
```

Si una variable no muestra ningún valor, no fue inicializada en esa terminal. Vuelva a ejecutar su asignación.

## Ejecutar una ruta guardada en una variable

PowerShell usa el operador `&`:

```powershell
& $vm --version
& $vm --help
```

Escribir únicamente `$vm` muestra la cadena de la ruta, pero no ejecuta el programa.

## Continuar un comando en varias líneas

El acento grave al final de la línea indica continuación:

```powershell
& $vm `
  --max-steps 100000 `
  $programa
```

No debe haber espacios después del acento grave. También puede escribir el comando completo en una sola línea.

## Validar sin ejecutar

```powershell
& $vm --check $programa
$LASTEXITCODE
```

Un resultado válido devuelve `LCERO_CHECK=VALID` y exit code 0. Esta operación reconoce la RI, verifica operandos y resuelve etiquetas, pero no ejecuta transiciones y no produce `PRINT`.

## Ejecutar con presupuesto

```powershell
& $vm --max-steps 100000 $programa
$LASTEXITCODE
```

Para `prueba_3_optimizada.lci`, la salida esperada es `60`, `40`, `5` y `4`; el estado es `HALT` con `k=318`.

El presupuesto se comprueba antes de la siguiente instrucción. `LIMIT` indica que se agotó esa cantidad de pasos, no demuestra que el programa sea infinito.

## Ejecutar mediante rutas relativas

Desde la raíz del proyecto:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" `
  --max-steps 100000 `
  ".\TestsParaVM\prueba_3_optimizada.lci"
```

La VM busca una ruta relativa primero desde el directorio actual. Si no existe, prueba la misma ruta desde los ancestros de la ubicación real del ejecutable. Una ruta absoluta nunca se reinterpreta.

## Crear una traza

```powershell
$traza = Join-Path $repo 'build\prueba_3_optimizada.log'
& $vm --max-steps 100000 --trace $traza $programa
Get-Content -LiteralPath $traza
```

La traza registra `pc`, `k`, opcode y cambios de estado. No se mezcla con la secuencia observable `O` generada por `PRINT`.

## Ejecutar las pruebas

Los ejemplos siguientes suponen que `$ctest` y `$build` fueron definidos como indica [`08-cmake-y-vcpkg.md`](08-cmake-y-vcpkg.md), y que Debug ya fue compilado.

Pruebas configuradas en CTest:

```powershell
& $ctest --test-dir $build -C Debug --output-on-failure
```

Los 40 contratos:

```powershell
& ".\scripts\test-contracts.ps1" -Configuration Debug
```

Los seis programas de `TestsParaVM`:

```powershell
& ".\scripts\test-compiler-outputs.ps1" -Configuration Debug
```

Un contrato particular:

```powershell
& ".\scripts\test-contracts.ps1" `
  -Configuration Debug `
  -CaseId 24_salida_parcial_antes_error
```

## Interpretar `$LASTEXITCODE`

| Código | Resultado |
|---:|---|
| 0 | `HALT` o `--check` válido |
| 2 | `ERROR` dinámico |
| 3 | `ERROR_DE_CARGA` |
| 4 | `LIMIT` |
| 5 | argumentos, ruta, apertura o recurso |

`$LASTEXITCODE` debe consultarse inmediatamente después del ejecutable o script cuyo resultado interesa.

## Capturar `stdout` y `stderr` por separado

```powershell
$salida = Join-Path $repo 'build\programa.stdout'
$diagnostico = Join-Path $repo 'build\programa.stderr'
$argumentos = '--max-steps 100000 "' + $programa + '"'

$proceso = Start-Process -FilePath $vm `
  -ArgumentList $argumentos `
  -NoNewWindow -Wait -PassThru `
  -RedirectStandardOutput $salida `
  -RedirectStandardError $diagnostico

$proceso.ExitCode
Get-Content -LiteralPath $salida
Get-Content -LiteralPath $diagnostico
```

`stdout` queda reservado a `PRINT`; `stderr` contiene estados y diagnósticos. En una consola ambos pueden aparecer visualmente intercalados aunque sean canales diferentes.

## Errores comunes de la terminal

- **`cmake` no se reconoce:** use `& $cmake` después de seguir [`08-cmake-y-vcpkg.md`](08-cmake-y-vcpkg.md).
- **`Join-Path` indica que `Path` es nulo:** la variable situada después de `-Path` no fue definida en la sesión actual.
- **Aparece `>>`:** PowerShell espera que termine un comando multilínea, una comilla o un paréntesis. Presione `Ctrl+C` y vuelva a escribirlo.
- **El `.exe` no existe:** compile Debug o Release antes de intentar ejecutarlo.
- **El `.lci` no existe:** compruebe su nombre mediante `Test-Path` y `Get-ChildItem`.
- **`ERROR_DE_CARGA`:** use `--check` y revise `LINE`, `CODE` y `MESSAGE`.

Para una ejecución completa del ejemplo consulte [`../GUIA_EJECUTAR_UN_ARCHIVO_LCI.md`](../GUIA_EJECUTAR_UN_ARCHIVO_LCI.md).

