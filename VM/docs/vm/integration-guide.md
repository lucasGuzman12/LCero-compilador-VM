# Integración con el compilador L-Cero

## Contrato de intercambio

La única frontera admitida es un archivo de texto `.lci` que cumpla `LCERO_IR 1`. El compilador y la VM deben poder construirse, probarse y ejecutarse por separado. La VM no solicita el `.lc` original ni reescribe el `.lci` emitido.

Un artefacto mínimo tiene esta forma:

```text
LCERO_IR 1
MEM 0
CONST R0, 5
PRINT R0
HALT
```

Requisitos del emisor:

1. La primera línea no vacía es exactamente `LCERO_IR 1`.
2. La segunda línea no vacía es `MEM n`, con `n` natural decimal canónico.
3. Solo se usan `R0`, `R1`, `R2` y `R3`.
4. Los opcodes son los 18 autorizados, en mayúsculas. No existen `NEG`, `NOP`, `PUSH` ni `CALL`.
5. Los inmediatos usan `0` o `-?[1-9][0-9]*`; no se admiten `-0`, signo `+` ni ceros iniciales.
6. Todo índice de memoria satisface `0 <= m < n`.
7. Cada destino de `JMP` o `JZ` nombra una etiqueta única y válida.
8. Debe existir al menos un `HALT` en el archivo, aunque la alcanzabilidad es una propiedad dinámica.
9. No se emiten comentarios ni caracteres no ASCII en el contenido RI.
10. LF y CRLF son aceptables como entrada. La salida observable de `PRINT` siempre usa LF.

Antes de ejecutar un archivo emitido, la integración puede usar:

```powershell
& ".\build\bin\Release\lcero-vm.exe" --check ".\salida\programa.lci"
& ".\build\bin\Release\lcero-vm.exe" --max-steps 100000 ".\salida\programa.lci"
```

Una validación correcta confirma forma y referencias; no garantiza que el flujo alcance `HALT` ni que evite errores dinámicos.

## Registros y spilling

La VM ofrece cuatro registros físicos. La asignación de variables temporales, análisis de interferencias y cualquier algoritmo de coloreo —incluido Chaitin— pertenecen al compilador.

Si el compilador realiza spilling, debe emitir explícitamente `STORE` y `LOAD`. Los slots reservados para spill forman parte de la cota total `MEM n` junto con cualquier otra memoria del programa. Por ejemplo, si se usan slots 0 a 7, la declaración mínima es `MEM 8`.

La VM no distingue un slot de usuario de un slot de spill: ambos son posiciones de `M`, comienzan sin valor y deben inicializarse antes de leerse.

## Etiquetas y direcciones

El compilador emite nombres `[A-Za-z_][A-Za-z0-9_]*` que no coincidan con opcodes reservados. Puede referenciarlos antes de declararlos. El validador realiza dos pasadas y los resuelve a índices de `P` 0-based.

El encabezado, `MEM` y cada `LABEL` no ocupan posiciones de `P` ni consumen pasos. Una etiqueta al final del archivo puede apuntar legalmente a `|P|`, pero saltar a ella produce dinámicamente caída sin `HALT`.

## Salida y códigos de proceso

Durante una ejecución normal:

- `stdout` contiene exclusivamente los decimales de `PRINT`, cada uno con LF;
- `stderr` contiene estado, pasos, `pc`, categoría y diagnósticos;
- exit 0 representa `HALT`, 2 `ERROR`, 3 `ERROR_DE_CARGA`, 4 `LIMIT` y 5 un problema de argumentos/entrada.

El integrador no debe comparar la consola combinada para validar resultados. Debe capturar ambos canales por separado y comparar `stdout` como bytes.

## Prueba de extremo a extremo futura

Cuando el compilador del equipo esté disponible, el flujo propuesto es:

```text
programa.lc
   │ compilador del grupo
   ▼
programa.lci
   │ lcero-vm --check
   ▼
RI válida
   │ lcero-vm [presupuesto]
   ▼
HALT + stdout esperado
```

Procedimiento reproducible sugerido:

1. fijar un `.lc` y su salida esperada en el repositorio del compilador;
2. ejecutar el compilador y conservar el `.lci` producido sin editarlo;
3. ejecutar `lcero-vm --check` sobre ese artefacto;
4. ejecutar la VM con un presupuesto documentado;
5. comparar exit code, estado y bytes de `stdout`;
6. archivar comandos, versiones y hashes de entrada/salida.

**Estado actual:** la ejecución completa del compilador continúa **PENDIENTE**, porque esta entrega no contiene su ejecutable, fuente `.lc` ni comando de compilación. Sin embargo, el 22/10/2026 se recibieron seis `.lci` que el equipo identificó como salidas reales del compilador: tres normales y tres optimizados. La VM validó y ejecutó los seis en Debug y Release, y sus bytes coincidieron con los resultados entregados. Esto verifica la compatibilidad de esos artefactos concretos, pero no reemplaza la futura prueba extremo a extremo que también ejecute el compilador.

El procedimiento y los resultados están en [`pruebas-del-compilador.md`](pruebas-del-compilador.md).

## Compatibilidad y cambios

La versión normativa continúa siendo `LCERO_IR 1`. Si el compilador necesita una forma textual no contemplada, no debe incorporarla unilateralmente: cualquier cambio observable de `.lci` requiere autorización y una versión contractual acordada.
