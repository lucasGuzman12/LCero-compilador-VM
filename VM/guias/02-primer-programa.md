# 2. Validar y ejecutar el primer programa

Ejecute los comandos desde la raíz del repositorio después de compilar Debug:

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
$vm = Join-Path $repo 'build\bin\Debug\lcero-vm.exe'
$programa = Join-Path $repo 'test\contracts\valid\01_hola_registros.lci'
```

## Comprobar la CLI

```powershell
& $vm --version
& $vm --help
```

## Validar sin ejecutar

```powershell
& $vm --check $programa
$LASTEXITCODE
```

Se espera `LCERO_CHECK=VALID` y código 0. `--check` reconoce la cadena del lenguaje formal de RI, resuelve etiquetas y valida operandos, pero no aplica ninguna transición: el contador de pasos no avanza y no hay `PRINT`.

## Ejecutar

```powershell
& $vm $programa
$LASTEXITCODE
```

Resultado observable esperado:

```text
5
```

También se informa por `stderr`:

```text
LCERO_STATUS=HALT LCERO_STEPS=3 PC=2 CATEGORY=TERMINACION
```

El `5` es `stdout` producido por `PRINT`; termina con un byte LF (`0A`). El estado y los diagnósticos se envían a `stderr` para no contaminar la salida del programa.

## Ejecutar una prueba del compilador

```powershell
& $vm --max-steps 100000 'TestsParaVM\prueba_1.lci'
```

Salida esperada:

```text
38
3
-3
26
```

La VM resuelve una ruta relativa primero desde el directorio de trabajo. Si no existe allí, asciende desde la ubicación real del ejecutable hasta encontrar la misma ruta. Por eso el siguiente uso también funciona después de calcular una única ruta absoluta para el ejecutable:

```powershell
Set-Location ([IO.Path]::GetTempPath())
& $vm --max-steps 100000 'TestsParaVM\prueba_1.lci'
```

Una ruta absoluta recibida como argumento se usa sin reinterpretarla. La búsqueda alternativa no inventa ubicaciones personales: parte del propio `.exe` descargado y compilado.

## Formato mínimo de un `.lci`

```text
LCERO_IR 1
MEM 0
CONST R0, 5
PRINT R0
HALT
```

El encabezado, `MEM` y `LABEL` son directivas y no cuentan como pasos. Solo las instrucciones ejecutables forman la secuencia `P`; `pc` es su índice 0-based y `k` cuenta instrucciones efectivamente ejecutadas.

