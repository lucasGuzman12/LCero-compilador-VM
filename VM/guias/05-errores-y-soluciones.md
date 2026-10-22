# 5. Errores y soluciones

## Estados y códigos de salida

| Código | Estado | Significado |
|---:|---|---|
| 0 | `HALT` o check válido | Terminación explícita o validación estática exitosa |
| 2 | `ERROR` | Fallo dinámico durante una transición |
| 3 | `ERROR_DE_CARGA` | El archivo no pertenece al formato `LCERO_IR 1` |
| 4 | `LIMIT` | Presupuesto agotado antes de la siguiente instrucción |
| 5 | error de CLI/recurso | Argumentos, ruta, apertura o recurso físico |

Después de una invocación, consulte `$LASTEXITCODE`. El diagnóstico incluye `LCERO_STATUS`, `LCERO_STEPS`, `PC` y `CATEGORY` en `stderr`.

## Problemas frecuentes

### No existe el ejecutable

Compile primero con la [guía 1](01-instalacion-y-compilacion.md). Verifique:

```powershell
Test-Path ".\build\bin\Debug\lcero-vm.exe"
```

### CMake no encuentra vcpkg

```powershell
Test-Path (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake')
```

Si devuelve `False`, `VCPKG_ROOT` está ausente o apunta a otra carpeta. No modifique CMake con una ruta personal; corrija la variable de entorno.

### La VM no encuentra el `.lci`

Compruebe la ruta elegida por PowerShell:

```powershell
$programa = (Resolve-Path -LiteralPath ".\TestsParaVM\prueba_1.lci").Path
Test-Path -LiteralPath $programa
```

Use comillas o variables cuando haya espacios. La VM no acepta `.lc`: ese archivo debe pasar antes por el compilador.

### `ERROR_DE_CARGA`

```powershell
& ".\build\bin\Debug\lcero-vm.exe" --check $programa
```

Revise `LINE`, `CODE` y `MESSAGE`. Las causas incluyen versión incorrecta, caracteres no ASCII, inmediato inválido, aridad incorrecta, registro fuera de `R0..R3`, slot fuera de `MEM n`, etiqueta duplicada/desconocida o ausencia de `HALT`.

### `ERROR`

El loader aceptó el programa, pero la rama ejecutada intentó dividir por cero, leer un registro/slot no inicializado o caer desde `pc=|P|`. Una instrucción que falla no se confunde con un error gramatical.

### `LIMIT`

Aumente el presupuesto solo si entiende el programa:

```powershell
& ".\build\bin\Debug\lcero-vm.exe" --max-steps 100000 $programa
```

No use `--no-step-limit` con un programa desconocido. Alcanzar `LIMIT` no resuelve el problema de la parada.

### La consola mezcla salida y diagnóstico

`stdout` y `stderr` pueden verse intercalados en pantalla aunque sean canales distintos. Los runners los capturan por separado y comparan `stdout` byte a byte. Consulte [`../docs/vm/troubleshooting.md`](../docs/vm/troubleshooting.md) para reproducción avanzada.

