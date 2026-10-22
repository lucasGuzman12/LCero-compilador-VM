# 6. Usar otros programas del compilador

## Contrato entre compilador y VM

El compilador debe entregar un archivo de texto `.lci` conforme a `LCERO_IR 1`. La VM no recibe `.lc`, tokens, AST ni objetos internos. Esta frontera permite cambiar la implementación del compilador sin acoplarla al intérprete, siempre que preserve la representación intermedia.

Un archivo emitido debe contener:

1. primera línea no vacía `LCERO_IR 1`;
2. segunda línea no vacía `MEM n`, con cota no negativa;
3. instrucciones de la ISA aprobada y directivas `LABEL` válidas;
4. al menos un `HALT` en el archivo.

La ISA ejecutable es: `CONST`, `LOAD`, `STORE`, `MOV`, `ADD`, `SUB`, `MUL`, `DIV`, `CMP_EQ`, `CMP_NE`, `CMP_LT`, `CMP_LE`, `CMP_GT`, `CMP_GE`, `JMP`, `JZ`, `PRINT` y `HALT`. No se deben emitir `NEG`, `NOP`, `PUSH`, `CALL` ni extensiones no acordadas.

## Flujo recomendado para un archivo nuevo

Desde la raíz del repositorio:

```powershell
$repo = (Resolve-Path -LiteralPath ".").Path
$vm = Join-Path $repo 'build\bin\Debug\lcero-vm.exe'
$programa = (Resolve-Path -LiteralPath ".\TestsParaVM\prueba_1.lci").Path
```

Reemplace únicamente el valor de `$programa` por el `.lci` generado y siga estas etapas:

```powershell
# 1. Validación estática: no ejecuta instrucciones.
& $vm --check $programa
if ($LASTEXITCODE -ne 0) { throw 'El compilador emitió una RI no válida.' }

# 2. Ejecución protegida.
& $vm --max-steps 100000 $programa
$LASTEXITCODE

# 3. Traza opcional para estudiar las transiciones.
& $vm --max-steps 100000 --trace (Join-Path $repo 'build\programa.log') $programa
```

## Qué comparar

- Compare `stdout` con el resultado esperado; `PRINT` serializa enteros decimales y LF.
- Compare el estado terminal (`HALT`, `ERROR` o `LIMIT`).
- No exija el mismo `k`, `pc`, uso de memoria o registros a dos compilaciones optimizadas si el contrato solo requiere preservar el comportamiento observable.
- `--check` exitoso no garantiza que una rama ejecutada no divida por cero ni lea un valor ausente.

Los registros comienzan sin valor y solo existen `R0..R3`. La memoria declarada por `MEM n` también comienza sin valores; un `STORE` inicializa un slot y `LOAD` exige que ya esté inicializado. Estas condiciones modelan el estado operacional, no son detalles del lenguaje fuente.

## Añadir un caso permanente

Guarde el `.lci` y su salida esperada sin editar el programa para acomodarlo a la VM. Si desea incorporarlo al runner de los seis casos, agregue explícitamente su nombre y bytes esperados en `scripts/test-compiler-outputs.ps1`. Nunca regenere automáticamente una expectativa a partir de una ejecución que todavía se está verificando.

