# 3. Pruebas incluidas

## Pruebas CTest

```powershell
ctest --test-dir .\build -C Debug --output-on-failure
ctest --test-dir .\build -C Release --output-on-failure
```

CTest conserva los unitarios del estado, loader, intérprete, CLI y trazas, además de la suite contractual. Ejecute Debug y Release de forma secuencial para que los runners no compitan por los mismos artefactos temporales.

## Los 40 contratos de `LCERO_IR 1`

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-contracts.ps1" -Configuration Debug

powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-contracts.ps1" -Configuration Release
```

El resumen correcto es:

```text
CONTRACT_TOTAL=40 PASS=40 FAIL=0 HALT=15 LIMIT=3 ERROR=6 ERROR_DE_CARGA=16
SUITE_RESULT=PASS
```

La clasificación distingue propiedades estáticas y dinámicas. Un `DIV` por cero puede pertenecer a un archivo sintácticamente válido y convertirse en `ERROR` solamente si la transición se ejecuta. En cambio, una etiqueta inexistente es `ERROR_DE_CARGA` y deja `k=0`.

Para un caso individual:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-contracts.ps1" `
  -Configuration Debug -CaseId 24_salida_parcial_antes_error
```

## Los 6 programas generados por el compilador

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-compiler-outputs.ps1" -Configuration Debug

powershell -NoProfile -ExecutionPolicy Bypass `
  -File ".\scripts\test-compiler-outputs.ps1" -Configuration Release
```

Resumen esperado: `COMPILER_OUTPUT_TOTAL=6 PASS=6 FAIL=0`.

| Programa | Salida esperada | Pasos normales/optimizados |
|---|---|---:|
| `prueba_1` | `38`, `3`, `-3`, `26` | 79 / 35 |
| `prueba_2` | `100`, `400`, `600`, `700` | 76 / 34 |
| `prueba_3` | `60`, `40`, `5`, `4` | 708 / 318 |

La igualdad de salida entre cada versión normal y optimizada es evidencia para esos programas concretos. No demuestra por sí sola que toda optimización posible preserve la semántica.

## Prueba rápida

```powershell
& ".\scripts\smoke.ps1"
```

Este script comprueba ayuda, versión, argumentos, carga válida, `HALT`, `LIMIT`, error dinámico y creación de una traza.

