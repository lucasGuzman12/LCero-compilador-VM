# Decisiones de ingeniería de la VM L-Cero

Estas decisiones organizan la implementación de Windows y su CLI. No cambian la sintaxis ni la semántica de `LCERO_IR 1`.

## D-001 — Plataforma y compilación

- C++17, MSVC x64 y generador CMake `Visual Studio 18 2026`.
- CMake mínimo 4.2.
- vcpkg funciona en modo manifiesto mediante `VCPKG_ROOT`; no se codifica una ruta personal en CMake.
- `boost-multiprecision` se importa con el target real `Boost::multiprecision`. Es una biblioteca de cabeceras y no agrega una DLL al ejecutable.
- Debug y Release se generan bajo `build/bin/<Configuración>/`.

## D-002 — Frontera del lenguaje

La VM acepta únicamente rutas a `.lci`; nunca interpreta `.lc`, tokens, AST ni estructuras del compilador. `LCERO_IR 1` es el contrato inmutable entre el compilador y la VM. `--check` abre y valida sin ejecutar; el modo posicional carga, valida y ejecuta la RI.

En Windows la extensión se compara sin distinguir mayúsculas y minúsculas. Las rutas se reciben mediante `wmain` y `std::filesystem::path`, por lo que conservan espacios y Unicode y se resuelven respecto del directorio de trabajo del proceso.

Para permitir que el repositorio se abra desde otro directorio, una ruta relativa inexistente en el CWD tiene una segunda resolución documentada: se prueba la misma ruta en cada ancestro de la ubicación real de `lcero-vm.exe`. El CWD conserva precedencia, una ruta absoluta nunca se reinterpreta y no se codifica ninguna cuenta local. Esta es una convención de localización de archivos del proceso anfitrión; no modifica la gramática ni la semántica de `LCERO_IR 1`.

## D-003 — Canales de salida

- En ejecuciones normales, `stdout` queda reservado exclusivamente a los valores decimales de `PRINT`, cada uno terminado en LF (`\n`). `wmain` configura el descriptor como binario para impedir la traducción Windows LF → CRLF.
- `--help` y `--version` son modos informativos, no ejecuciones de RI, y escriben en `stdout`.
- Los errores de argumentos, archivo y disponibilidad de fase escriben en `stderr` y no contaminan `stdout`.
- Los estados usan una línea estable como `LCERO_STATUS=HALT LCERO_STEPS=3 PC=2 CATEGORY=TERMINACION` en `stderr`. Las categorías de proceso son `TERMINACION`, `PRESUPUESTO`, `EJECUCION`, `CARGA` y `RECURSOS`. En `ERROR` se agregan `LINE` cuando corresponde, `CODE` y `MESSAGE`.
- En etapa 00 se usa `LCERO_CLI_ERROR=<CATEGORIA> <detalle>`. No se emite `LCERO_STATUS`, porque no hubo carga ni transición de la máquina abstracta.
- `--check` válido emite `LCERO_CHECK=VALID` en `stderr`. Un contenido inválido emite `LCERO_STATUS=ERROR_DE_CARGA LCERO_STEPS=0 PC=0 CATEGORY=CARGA`, `LINE` cuando existe una línea responsable, `CODE` y `MESSAGE`. Los ceros certifican que no se ejecutó ninguna instrucción y `stdout` permanece vacío.
- Un fallo físico anterior a crear la máquina —apertura, lectura, tamaño o memoria durante carga— usa `LCERO_CLI_ERROR=RECURSO` y exit 5. Un recurso agotado durante una transición usa el estado `ERROR`, categoría `RECURSOS` y exit 2, conservando `pc`, `k` y la salida previa cuando todavía pueden representarse. Un fallo al escribir `stdout` también invalida el éxito del proceso y se informa de esa forma.

## D-004 — Códigos de proceso

Convenciones propias de esta implementación, no reglas del lenguaje fuente ni de la ISA:

| Código | Significado |
|---:|---|
| 0 | `HALT`, `--check` válido, ayuda o versión correctas |
| 2 | `ERROR` de ejecución |
| 3 | `ERROR_DE_CARGA` |
| 4 | `LIMIT` |
| 5 | argumentos inválidos o archivo inexistente/inaccesible |

`--max-steps 0` es un presupuesto válido, mientras que `--no-step-limit` debe ser explícito; no pueden combinarse. El límite predeterminado es 100000 instrucciones.

## D-005 — Gramática estable de CLI

Se admiten exactamente una de estas formas:

```text
lcero-vm.exe --help
lcero-vm.exe --version
lcero-vm.exe --check RUTA.lci
lcero-vm.exe [--max-steps N | --no-step-limit] [--trace RUTA.log] RUTA.lci
```

Las opciones de ejecución pueden preceder o seguir a la única ruta posicional. `--check` no admite opciones de presupuesto ni `--trace`, y termina con 0 para RI válida, 3 para contenido inválido y 5 para entrada inaccesible o fallo físico. `N` usa la forma `0` o `[1-9][0-9]*` y debe caber en `uint64_t`, el tipo físico de `k`. `--trace` se admite una sola vez, consume la ruta siguiente y esa ruta no puede usar la extensión reservada `.lci`.

## D-006 — Tipos numéricos y formas decimales

El dominio `Integer` es `boost::multiprecision::cpp_int`. La conversión de entrada acumula cada dígito directamente sobre ese tipo; no usa `stoi`, `stoll`, `long double` ni un entero intermedio de ancho fijo. La salida decimal se obtiene directamente desde `cpp_int`.

Se mantienen analizadores separados porque representan categorías distintas del lenguaje de RI:

- Inmediato: `0` o `-?[1-9][0-9]*`. Rechaza `-0`, signo `+` y ceros iniciales.
- Declaración `MEM`: `0` o `[1-9][0-9]*`. Es una cota lógica no negativa y canónica.
- Índice de slot: `0` o `[1-9][0-9]*`. Además de su sintaxis, al operar debe satisfacer `0 <= m < MEM`.

Esta elección vuelve inequívocas las formas textuales sin modificar el conjunto de valores matemáticos del contrato. Si el documento maestro congela en el futuro otra normalización para `MEM`, será necesario solicitar autorización antes de cambiar el comportamiento observable.

## D-007 — Representación de C=(P,pc,R,M,O,k)

- `P` es `Program`, dueño de una secuencia sin mutadores públicos. Sus elementos son `Instruction` con opcode tipado, operandos mediante `variant` y línea `.lci`; `LABEL`, el encabezado y `MEM` no forman parte de esta secuencia.
- `pc` usa `size_t` porque indexa el vector físico de instrucciones y puede valer `|P|`; no puede superarlo.
- `R` es un arreglo de cuatro `optional<cpp_int>`. Ausencia y valor cero son estados diferentes.
- `M` conserva la cota lógica como `cpp_int` y materializa únicamente slots escritos en `map<cpp_int,cpp_int>`. Por ello `MEM n` no reserva físicamente `n` objetos.
- `O` es una secuencia de `cpp_int`. Se agrega incrementalmente y puede renderizarse con LF sin escribir por sí misma en `stdout`.
- `k` es un contador físico `uint64_t`, inicialmente cero y monótono. Un desbordamiento representacional es un error interno explícito, no aritmética modular silenciosa.
- El resultado operacional se representa con `RUNNING`, `HALT`, `ERROR`, `ERROR_DE_CARGA` o `LIMIT`; `RUNNING` describe la ausencia de terminación y no es una instrucción ni un estado terminal nuevo del contrato.

Las condiciones conocidas se devuelven mediante `Result<T>` u `OperationResult`. Los accesos inválidos o no inicializados no dependen de excepciones sin tratar.

## D-008 — Lectura física y límites de recursos

- El archivo se abre en binario mediante `std::filesystem::path`, conservando rutas Unicode de Windows.
- Se aceptan LF y CRLF. Un CR aislado es inválido y conserva su número de línea.
- El texto admite bytes ASCII visibles y tabulación horizontal como separador. NUL, controles restantes, DEL y bytes UTF-8 no ASCII se rechazan como contenido de carga.
- La cota física es 16 MiB. Superarla es `ERROR_RECURSO`, no `ERROR_DE_CARGA`.
- Fallos de apertura/lectura y agotamiento de memoria se categorizan separadamente de la gramática.

La cota de 16 MiB protege el proceso y no limita el valor matemático de `MEM n`: este continúa como `cpp_int` y no causa una reserva masiva.

## D-009 — Gramática superficial y resolución de etiquetas

El parser de RI es manual y específico para `.lci`; no es un lexer del lenguaje fuente ni usa generadores o expresiones regulares como sustituto del contrato.

1. Se omiten líneas vacías y se exige `LCERO_IR 1` como primera línea no vacía.
2. La segunda línea no vacía es `MEM n`, usando la forma canónica de D-006.
3. Cada línea restante es una instrucción o `LABEL identificador`. Los operandos se separan por comas, permiten espacios/tabulaciones alrededor y prohíben espacios internos.
4. La primera pasada tipa operandos, comprueba aridad/registros/slots, recolecta `LABEL -> pc` y excluye directivas de `P`.
5. La segunda pasada reemplaza referencias de salto por `JumpTarget` 0-based. Una etiqueta al EOF puede resolver a `|P|`; una referencia ausente siempre falla en carga.
6. Debe existir al menos un `HALT`, alcanzable o no. Errores como división por cero o lectura no inicializada no se evalúan durante `--check`.

Los nombres de etiqueta cumplen `[A-Za-z_][A-Za-z0-9_]*`, son sensibles a mayúsculas, únicos y no pueden coincidir con un mnemonic ejecutable de la ISA.

## D-010 — Relación de transición y terminación

- Cada transición consulta la instrucción tipada `P[pc]`; no vuelve a tokenizar el archivo. Los operandos fuente se copian antes de escribir el destino, de modo que los alias como `ADD R0,R0,R1` son válidos.
- `CONST`, `LOAD`, `STORE`, `MOV`, aritmética, comparaciones y `PRINT` avanzan `pc` una posición. `JMP` sustituye `pc` por su destino y `JZ` selecciona destino o sucesor. `HALT` deja `pc` en su propio índice.
- Las comparaciones escriben el entero matemático 1 o 0. `cpp_int` efectúa `DIV` entera con truncamiento hacia cero; el divisor cero produce `ERROR`.
- Una instrucción completada incrementa `k`. Un intento que falla antes de completar su efecto —por registro/slot ausente o división por cero— no incrementa `k`. Esta es la convención operacional elegida para la ambigüedad anotada en `expected_cases.json`, donde los errores dinámicos no fijan un valor esperado de pasos.
- El presupuesto se comprueba antes de una instrucción existente. Si `k >= N`, el resultado es `LIMIT`; por eso `N=0` limita antes del primer paso y un `HALT` ejecutado como paso N termina en `HALT`. Si `pc=|P|`, se informa caída sin `HALT` porque no existe una próxima instrucción a presupuestar.
- `PRINT` agrega a `O` en orden y la salida acumulada se conserva si una transición posterior termina en `ERROR` o si se alcanza `LIMIT`.
- Ninguna instrucción se ejecuta después de un `ERROR_DE_CARGA`. Un fallo de memoria física o de escritura del proceso se informa como recurso, fuera de la semántica de la RI.
- `CATEGORY` es una clasificación de diagnóstico de esta CLI y no agrega estados ni opcodes a `LCERO_IR 1`.

## D-011 — Traza de diferencias y observabilidad

- `--trace RUTA` habilita un observador de diagnóstico. La ruta puede ser absoluta o relativa al directorio de trabajo, admite espacios y Unicode y se abre después de validar toda la RI pero antes de construir la ejecución.
- El archivo existente se trunca. Se escribe mediante `std::ofstream` en modo binario; el formato textual usa UTF-8 (actualmente su subconjunto ASCII) y LF exacto. Nunca se escribe en `stdout` ni se reutiliza la gramática `.lci`.
- El encabezado `LCERO_TRACE 1` fija la versión del log, declara la política `mode=DIFF`, describe `|P|`, `MEM`, conserva `LABEL -> pc` y muestra el estado inicial vacío.
- Cada instrucción genera `BEFORE` con `k`, `pc`, índice ejecutable, línea fuente, opcode, operandos y registros, seguido por `AFTER` con estado y el cambio relevante. Los diffs muestran escritura de registro, slot materializado, destino efectivo de salto, elemento agregado a `O` o `HALT`; no duplican toda la memoria dispersa en cada paso.
- `TERMINAL` registra `HALT`, `ERROR` o `LIMIT`. Un error conserva las líneas previas y el estado parcial. Para `LIMIT`, el texto declara expresamente que agotar el presupuesto no demuestra no terminación.
- `ExecutionState::registers()` y `initialized_memory()` son vistas `const`; permiten inspección en Visual Studio y trazas sin abrir mutadores externos. `ValidatedIr::labels` conserva la tabla de símbolos ya resuelta para relacionar nombres con índices de `P`.
- Si el log no puede abrirse, no comienza la ejecución: se informa `LCERO_CLI_ERROR=TRAZA`, código de proceso 5 y `stdout` vacío. Si falla una escritura física después de comenzar, la salida `PRINT` ya producida se conserva y el proceso termina como `ERROR`, categoría `RECURSOS`, exit 2; no se falsea un éxito.

## Aspectos aún no definidos

No se define un formato JSON ni se promete compatibilidad de la traza con herramientas externas. Cualquier futura versión del log debe distinguirse de `LCERO_TRACE 1`. Esto no modifica el formato observable `.lci`.

## D-012 — Automatización contractual y timeouts

- `test/contracts/expected_cases.json` es la fuente inmutable de estado, presupuesto, pasos fijados y archivo de salida para los 40 fixtures. PowerShell lo lee con `ConvertFrom-Json`; el ejecutable C++ no incorpora una biblioteca JSON.
- Un `max_steps` numérico se pasa literalmente como `--max-steps N`. Los casos finitos con `null` usan el presupuesto predeterminado. El caso 17 siempre recibe N=4 y nunca se ejecuta desprotegido.
- Cada proceso hijo se ejecuta mediante `System.Diagnostics.Process`; `stdout` y `stderr` se copian simultáneamente a archivos distintos para evitar deadlocks. `stdout` se compara como bytes y se rechaza cualquier CR `0D`.
- El timeout externo predeterminado es 10 segundos por proceso y CTest aplica 120 segundos a la suite. Estas cotas son defensas de infraestructura, no pasos de la máquina ni pruebas de no terminación.
- `error_class_expected` es descriptivo. `LECTURA_NO_INICIALIZADA` agrupa registros y slots ausentes; `FORMATO_INVALIDO` puede corresponder al diagnóstico preciso `OPERANDO_INVALIDO`; `ETIQUETA_INVALIDA` puede corresponder a `ETIQUETA_RESERVADA`. Los mapeos se informan, no reescriben el manifiesto.
- Ante un fallo, el runner termina distinto de cero, imprime el motivo en la línea del caso y conserva capturas bajo `FAILED_ARTIFACTS`. Nunca regenera ni actualiza automáticamente archivos `.out`.
- La distribución comprobada es 14 archivos en `valid/`, 4 en `limit/`, 6 en `runtime_error/` y 16 en `load_error/`; los estados esperados suman 15 `HALT`, 3 `LIMIT`, 6 `ERROR` y 16 `ERROR_DE_CARGA`.

Si cualquiera de estos puntos cambia algo observable del formato `.lci`, debe solicitarse autorización antes de implementarlo.

## D-013 — Entrega independiente e integración

- La unidad entregable es la VM y su contrato de archivo `.lci`; no se incorpora ni simula el compilador del lenguaje fuente.
- CMake, presets, scripts y documentación usan rutas relativas o `VCPKG_ROOT`. Ningún archivo fuente fija la ruta de una cuenta local.
- `build/`, incluida la solución `LCeroVM.slnx` observada (o `.sln` en otros generadores), es regenerable y no se versiona.
- La prueba extremo a extremo `.lc -> compilador -> .lci -> VM` queda definida, pero permanece pendiente mientras el compilador del grupo no esté disponible.
- Las mediciones del Hito III se documentan como plan separado. No se publican tiempos, memoria ni conclusiones asintóticas que no hayan sido medidos y analizados.
- La versión de la aplicación puede avanzar sin cambiar la versión normativa `LCERO_IR 1`. Cualquier cambio de la RI requiere autorización contractual independiente.

Los alcances excluidos continúan siendo GUI, bytecode binario, generación nativa, interpretación de `.lc` y una afirmación de completitud de Turing sin demostración constructiva.
