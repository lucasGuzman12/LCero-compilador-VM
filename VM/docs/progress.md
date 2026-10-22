# Progreso de la máquina virtual L-Cero

## Etapa 00 — Preparación de Windows, solución y CLI mínima

- **Fecha de cierre:** 19/10/2026.
- **Alcance:** proyecto CMake C++17 para Visual Studio 2026 x64, manifiesto vcpkg, dependencia `cpp_int`, CLI mínima, test CTest y scripts PowerShell.
- **Estado:** VERIFICADO.
- **Evidencia:** el reintento descargó e instaló `boost-math` y `boost-multiprecision` 1.91.0; CMake configuró con exit 0. Debug y Release compilaron con exit 0. CTest Debug pasó 1/1 y el smoke pasó ayuda, versión y opción inválida con exit global 0.
- **Corrección durante la verificación:** `smoke.ps1` ahora captura el `stderr` nativo esperado del caso negativo sin convertirlo en una excepción de Windows PowerShell y devuelve exit 0 al completar todos los casos.

## Etapa 01 — Estado operacional, instrucciones y enteros arbitrarios

- **Fecha de cierre:** 19/10/2026.
- **Alcance:** `Instruction`, `Program`, operandos fuertes, conversiones decimales exactas, diagnósticos explícitos y estado `C=(P,pc,R,M,O,k)` con memoria dispersa.
- **Estado:** VERIFICADO.
- **Fuera de alcance respetado:** no se implementaron loader, validador, lectura de `.lci`, despacho de opcodes, límites de ejecución ni trazas.
- **Pruebas:** Debug y Release compilaron con exit 0. CTest pasó 2/2 en Debug y 2/2 en Release. El ejecutable detallado `lcero_vm_model_tests.exe` terminó con exit 0 y verificó todos los criterios de aceptación de esta fase. El smoke de CLI volvió a terminar con exit 0. `dumpbin /dependents` confirmó que Release no depende de DLL de Boost; `cpp_int` permanece como dependencia de cabeceras.
- **Casos destacados:** R0 ausente se distingue de R0=0; en `MEM 2`, 0 y 1 están en rango y 2 no; un slot vacío se distingue de cero; positivos y negativos de más de 100 dígitos conservan su forma exacta; modificar una copia no cambia el original; `pc`, `k`, `O` y el estado terminal respetan sus invariantes.
- **Fixtures:** los 81 archivos bajo `test/contracts/` conservan los mismos SHA-256 que el paquete suministrado.
- **Problemas conocidos:** MSBuild `FileTracker` recibe `E_ACCESSDENIED` dentro del aislamiento del agente; las compilaciones reales se ejecutaron fuera de ese aislamiento y finalizaron correctamente. La primera descarga de Boost había fallado con `curl` 56, pero el reintento solicitado la completó.
- **Verificación de IDE pendiente/manual:** se documentó cómo abrir la carpeta CMake, seleccionar `x64-Debug` y colocar un breakpoint; no se afirma haber realizado una sesión gráfica en Visual Studio.
- **Siguiente fase sugerida:** etapa 02, loader y validador de `.lci`, únicamente después de la confirmación del usuario.

Hasta esta etapa no se afirma ejecución de la ISA. Tampoco se declara probada la completitud de Turing.

## Etapa 02 — Loader, validador, etiquetas y `--check`

- **Fecha de cierre:** 19/10/2026.
- **Alcance:** lectura física segura, ASCII/LF/CRLF, parser superficial de RI, validación estricta de aridad y operandos, tabla de símbolos en primera pasada, resolución de saltos en segunda pasada y CLI `--check`.
- **Estado:** VERIFICADO.
- **Compilación:** configuración CMake, Debug y Release terminaron con exit 0 usando Visual Studio 18 2026 x64 y Boost.Multiprecision 1.91.0.
- **CTest:** 3/3 tests pasaron en Debug y 3/3 en Release: CLI, modelo operacional y loader/validador. El flujo completo `scripts/build-debug.ps1` también terminó con exit 0.
- **Conformidad estática ejecutada:** 24/24 fixtures bien formados fueron aceptados (`valid` 14, `limit` 4 y `runtime_error` 6); 16/16 fixtures `load_error` fueron rechazados como `ERROR_DE_CARGA`.
- **Casos propios ejecutados:** LF válido, CRLF válido, CR aislado inválido, `LCERO_IR 2` inválido, archivo inexistente como recurso y archivo mayor de 16 MiB como recurso.
- **Resolución comprobada:** en `08_bucle_descendente`, `fin -> 13` y `ciclo -> 2`; en `23_caida_sin_halt`, la etiqueta final resuelve legalmente a `|P|`.
- **CLI comprobada:** `--check` válido exit 0; etiqueta inexistente exit 3 con línea/código; entrada posicional sin intérprete exit 6; smoke completo exit 0.
- **Fixtures:** no se modificó ningún resultado esperado ni archivo `.lci`; sus 81 archivos continúan idénticos al paquete de origen.
- **Problemas conocidos:** MSBuild `FileTracker` necesita ejecutarse fuera del aislamiento del agente; Debug y Release reales finalizaron correctamente con ese permiso. La distribución real del paquete es 14 `valid` + 4 `limit` + 6 `runtime_error` = 24, aunque el texto del prompt la describa como 15 + 3 + 6.
- **Siguiente fase sugerida:** etapa 03, intérprete de la ISA, únicamente después de la confirmación del usuario.

Esta etapa no ejecuta instrucciones, no produce `PRINT` ni `STEPS`, y no convierte errores dinámicos como división por cero en errores de carga.

## Etapa 03 — Intérprete completo de la ISA y salida exacta

- **Fecha de cierre:** 20/10/2026.
- **Alcance:** ejecución real de los 18 opcodes autorizados sobre `Program` tipado e inmutable; aritmética y comparaciones con `cpp_int`; memoria dispersa; saltos; `PRINT`; `HALT`; errores dinámicos; límite predeterminado, `--max-steps` y `--no-step-limit`.
- **Estado:** VERIFICADO.
- **Compilación:** la configuración CMake detectó CMake 4.3.1-msvc1, Visual Studio 18 2026, MSVC 14.51, Windows SDK 10.0.26100.0 y Boost.Multiprecision 1.91.0. Debug y Release terminaron con exit 0 y sin advertencias del proyecto.
- **CTest:** 4/4 tests pasaron en Debug y 4/4 en Release: CLI, modelo operacional, loader/validador e intérprete. `scripts/build-debug.ps1` y `scripts/smoke.ps1` terminaron con exit 0.
- **Conformidad dinámica:** los 14 casos `valid` alcanzaron `HALT` con salida y `k` esperados; los 4 casos `limit` respetaron el presupuesto; los 6 casos `runtime_error` terminaron en `ERROR` con la causa esperada y conservaron la salida parcial. La prueba también confirmó cobertura de los 18 opcodes ejecutables.
- **Casos destacados:** `01_hola_registros` produjo `5\n`, `k=3`; `03_division_entera_negativa` produjo `-3\n`; `08_bucle_descendente` produjo `3\n2\n1\n`, `k=40`; `14_halt_solo` terminó con salida vacía y `k=1`; el límite predeterminado detuvo el bucle de prueba exactamente en 100000 pasos.
- **Salida Windows:** una ejecución redirigida de `08_bucle_descendente` produjo bytes `33-0A-32-0A-31-0A`, idénticos al `.out` esperado. `24_salida_parcial_antes_error` produjo `38-0A`, también idéntico, antes de `DIVISION_POR_CERO`. No apareció CR (`0D`).
- **CLI:** ayuda y versión exit 0; `HALT` exit 0; `ERROR` dinámico exit 2; `ERROR_DE_CARGA` exit 3; `LIMIT` exit 4; argumentos o recursos de entrada exit 5. Los estados finales se emiten en `stderr` y `stdout` contiene solo `PRINT` durante ejecución.
- **Convención resuelta:** una instrucción que falla antes de completar su efecto no incrementa `k`; `expected_cases.json` deja ese valor sin fijar para los errores dinámicos. La caída con `pc=|P|` es `ERROR` y no terminación implícita.
- **Fixtures:** no se modificaron los `.lci`, `expected_cases.json` ni los `.out` suministrados.
- **Problemas conocidos:** MSBuild `FileTracker` continúa recibiendo `E_ACCESSDENIED` dentro del aislamiento del agente. El fallo se reprodujo y las compilaciones reales Debug/Release se reintentaron fuera de ese aislamiento con exit 0. No se realizó una sesión gráfica ni se observó un breakpoint en Visual Studio; los pasos se documentan en README.
- **Siguiente fase sugerida:** etapa 04, únicamente después de la confirmación del usuario. No se implementaron trazas ni optimizaciones fuera de esta fase.

Esta etapa demuestra conformidad con los casos ejecutados de `LCERO_IR 1`; no declara probada la completitud de Turing.

## Etapa 04 — Errores, presupuesto de pasos y robustez de CLI

- **Fecha de cierre:** 20/10/2026.
- **Alcance:** clasificación inequívoca de `HALT`, `LIMIT`, `ERROR`, `ERROR_DE_CARGA`, errores de CLI y recursos físicos; métricas terminales; validación completa previa; conservación de salida parcial; endurecimiento de excepciones y argumentos.
- **Estado:** VERIFICADO.
- **Compilación:** CMake 4.3.1-msvc1 configuró Visual Studio 18 2026 x64 con MSVC 14.51, Windows SDK 10.0.26100.0 y Boost.Multiprecision 1.91.0. Debug y Release compilaron con exit 0 y sin advertencias del proyecto.
- **CTest:** 5/5 pruebas pasaron en Debug y 5/5 en Release. Se conservaron CLI, modelo, loader e intérprete y se agregó `lcero_vm_contract_cli`, que inicia el `.exe` por separado para cada caso del manifiesto.
- **Matriz contractual ejecutada:** 40/40 fixtures pasaron mediante la CLI real: 15 `HALT`, 3 `LIMIT`, 6 `ERROR` dinámicos y 16 `ERROR_DE_CARGA`. Cada salida se comparó byte a byte con su `.out`; los presupuestos se tomaron de `expected_cases.json`.
- **Presupuestos:** caso 15 terminó `HALT`, `k=3`; caso 16 terminó `LIMIT`, `k=2` y conservó `7\n`; caso 17 se ejecutó exclusivamente con `N=4` y terminó `LIMIT`, `k=4`; caso 18 terminó `LIMIT`, `k=0` antes de `HALT`.
- **Errores dinámicos:** los casos 19–24 conservaron `ERROR`, `pc`, `k`, categoría `EJECUCION`, diagnóstico y salida previa. No se inventó `steps_expected` para los seis valores `null` del manifiesto.
- **Errores de carga:** los 16 casos informaron `ERROR_DE_CARGA LCERO_STEPS=0 PC=0 CATEGORY=CARGA`, exit 3 y salida vacía. En particular, la etiqueta inexistente del caso 30 no inició la ejecución.
- **Robustez de proceso:** `--max-steps -1`, un límite no numérico, opciones incompatibles y una ruta inexistente devolvieron exit 5 sin `HALT`. Un stream de salida que rechaza bytes se probó como `ERROR`, categoría `RECURSOS`, exit 2. `std::bad_alloc` se captura durante creación de estado, transiciones, serialización y frontera de `wmain`.
- **Salida exacta:** caso 16 produjo `37-0A`, caso 24 produjo `38-0A` y caso 30 produjo cero bytes; los tres coincidieron con sus expectativas. `stdout` sigue reservado exclusivamente a `PRINT`.
- **Scripts:** `scripts/build-debug.ps1` terminó con exit 0 y 5/5 tests; `scripts/smoke.ps1` terminó con exit 0 y comprobó 20 escenarios de ayuda, argumentos, carga, presupuestos y errores dinámicos.
- **Política de pasos:** una instrucción que falla antes de completar su efecto no incrementa `k`. `LIMIT` solo certifica agotamiento del presupuesto observado; no se implementó detector de parada ni se afirma infinitud.
- **Fixtures:** no se modificaron los 81 archivos suministrados bajo `test/contracts/`, incluido el manifiesto y las salidas esperadas.
- **Problemas conocidos:** MSBuild `FileTracker` requiere ejecución fuera del aislamiento del agente por `E_ACCESSDENIED`; las compilaciones reales Debug y Release terminaron correctamente con ese permiso. No se realizó una sesión gráfica en Visual Studio.
- **Siguiente fase sugerida:** etapa 05 únicamente después de recibir y confirmar el siguiente prompt del usuario. No se avanzó a trazas ni optimización.

Esta fase endurece la observación operacional; no cambia la sintaxis `.lci`, la ISA ni la frontera con el compilador, y no declara probada la completitud de Turing.

## Etapa 05 — Trazas depurables, breakpoints y explicabilidad

- **Fecha de cierre:** 21/10/2026.
- **Alcance:** opción `--trace`, log binario UTF-8/LF basado en diferencias, estado inicial, pares `BEFORE`/`AFTER`, terminales `HALT`/`LIMIT`/`ERROR`, tabla observable de etiquetas y vistas `const` de registros y memoria dispersa. Se documentaron transiciones y una sesión reproducible de Visual Studio.
- **Estado:** VERIFICADO.
- **Compilación:** CMake 4.3.1-msvc1 configuró Visual Studio 18 2026 x64 con MSVC 14.51, Windows SDK 10.0.26100.0 y Boost.Multiprecision 1.91.0. Debug y Release compilaron con exit 0. Los ejecutables quedaron en `build/bin/Debug/lcero-vm.exe` y `build/bin/Release/lcero-vm.exe`.
- **CTest:** 6/6 pruebas pasaron en Debug y 6/6 en Release. Se agregó `lcero_vm_trace`; `lcero_vm_contract_cli` volvió a ejecutar sin cambios los 40 casos contractuales y a comparar sus salidas byte a byte.
- **Equivalencia observable:** `01_hola_registros` terminó con exit 0 y bytes `35-0A` tanto sin traza como con ella, idénticos a `expected/01_hola_registros.out`. Ambos modos informaron `HALT`, `k=3`, `pc=2`. El log no contuvo bytes CR.
- **Saltos y líneas fuente:** `08_bucle_descendente` mantuvo `3\n2\n1\n` y `k=40`. Su encabezado registró `ciclo -> pc 2` y `fin -> pc 13`; cada transición relaciona `exec_index` 0-based con `source_line` del `.lci`, y los saltos muestran el cambio efectivo de `pc`.
- **Terminales observados:** `17_bucle_bajo_limite` se ejecutó solamente con `--max-steps 4`, produjo `LIMIT`, `k=4` y anotó que el presupuesto no prueba no terminación. `24_salida_parcial_antes_error` conservó bytes `38-0A`, trazó el `DIV` fallido con `diff=[none]` y terminó `ERROR`, `k=3`, `pc=3`.
- **Ruta inaccesible:** un log cuyo directorio padre no existe devolvió exit 5 con `LCERO_CLI_ERROR=TRAZA CODE=TRAZA_INACCESIBLE`; `stdout` quedó vacío y la RI no comenzó a ejecutarse.
- **Medición orientativa:** en una ejecución Debug de `08_bucle_descendente`, la prueba registró 1587 μs sin trace y 6197 μs con trace. Es una muestra local dependiente de E/S, no un requisito de rendimiento ni un umbral de aceptación.
- **Scripts:** `scripts/build-debug.ps1` terminó con exit 0 y 6/6 tests. `scripts/smoke.ps1` terminó con exit 0 y verificó 23 escenarios, incluidos log en una ruta con espacios, LF exacto, terminal `HALT k=40` y apertura inaccesible.
- **Documentación:** `docs/vm/debugging.md` describe configuración, breakpoints y Watch en Visual Studio Community 2026. `docs/vm/operational-semantics.md` formaliza las transiciones solicitadas y aclara que las pruebas no demuestran completitud de Turing. La mini-guía incorpora ejecución y comparación con/sin traza.
- **Fixtures:** se conservaron los 81 archivos suministrados bajo `test/contracts/`; no se alteraron `.lci`, expectativas ni salidas para hacer pasar pruebas.
- **Problemas conocidos:** MSBuild `FileTracker` requiere ejecución fuera del aislamiento del agente por `E_ACCESSDENIED`; las compilaciones reales Debug/Release se ejecutaron con el permiso necesario y finalizaron con exit 0. La configuración y los breakpoints de Visual Studio están documentados, pero no se afirma haber realizado una sesión gráfica del IDE.
- **Siguiente fase sugerida:** ninguna se inició. Se espera confirmación expresa del usuario antes de cualquier etapa posterior.

La traza es un observador de diagnóstico y no una extensión de la ISA ni un segundo formato ejecutable. Esta etapa no cambia `LCERO_IR 1`, no interpreta `.lc` y no declara probada la completitud de Turing.

## Etapa 06 — Suite automática de 40 fixtures y regresión

- **Fecha de cierre:** 21/10/2026.
- **Alcance:** runner PowerShell único para el manifiesto contractual, informe de una línea por fixture, comparación binaria, timeout externo, integración CTest/Visual Studio, ejecución selectiva y pruebas adicionales de plataforma y estado.
- **Estado:** VERIFICADO.
- **Implementación:** `scripts/test-contracts.ps1` lee `expected_cases.json` mediante `ConvertFrom-Json`, ejecuta el `.exe` real con `System.Diagnostics.Process`, consume `stdout` y `stderr` simultáneamente y limita cada proceso a 10 segundos. CTest registra la suite con etiquetas `contract;regression` y timeout global de 120 segundos.
- **Matriz contractual:** Debug y Release informaron `CONTRACT_TOTAL=40 PASS=40 FAIL=0 HALT=15 LIMIT=3 ERROR=6 ERROR_DE_CARGA=16`, ambos con `SUITE_RESULT=PASS` y exit 0. Se emitieron exactamente 40 líneas `[NN/40]`, una por fixture.
- **Distribución física comprobada:** 14 `.lci` en `valid/`, 4 en `limit/`, 6 en `runtime_error/` y 16 en `load_error/`. El caso 15 aportó el decimoquinto `HALT` desde `limit/`; el caso 17 recibió siempre `--max-steps 4`.
- **Bytes y pasos:** cada `stdout` coincidió byte a byte con su `.out` y ninguno incorporó CR `0D`. Los valores `steps_expected` no nulos coincidieron; los errores dinámicos con `null` exigieron un `k` observable sin inventar una expectativa. Los 16 errores de carga certificaron `k=0`, `pc=0`, categoría `CARGA` y salida vacía.
- **Regresiones adicionales:** pasaron una ruta Unicode con espacios, un inmediato exacto de 100 dígitos, presupuestos N=0, N=1 y N=3, y dos ejecuciones sucesivas que demostraron que `R0` vuelve a no inicializado. La prueba C++ de CLI también invoca dos archivos dentro del mismo proceso de test para descartar estado global compartido.
- **Compilación:** Debug y Release compilaron con Visual Studio 18 2026/MSVC 14.51, CMake 4.3.1-msvc1, Windows SDK 10.0.26100.0 y Boost.Multiprecision 1.91.0; ambos comandos terminaron con exit 0. Los ejecutables son `build/bin/Debug/lcero-vm.exe` y `build/bin/Release/lcero-vm.exe`, versión 0.6.0.
- **CTest:** Debug pasó 6/6 en 4.24 s y Release 6/6 en 4.03 s. Un reintento completo mediante `scripts/build-debug.ps1` también pasó 6/6 con exit 0. `scripts/smoke.ps1` conservó sus 23 escenarios y terminó con exit 0.
- **Portabilidad de invocación:** desde la carpeta padre, el runner recibió `-ProjectRoot` absoluto y reprodujo correctamente `30_etiqueta_inexistente` como `ERROR_DE_CARGA`, `k=0`, salida vacía y exit global 0.
- **Prueba del mecanismo de fallo:** se sustituyó temporalmente el ejecutable por una herramienta ajena, sin tocar fixtures ni expectativas. La línea del caso informó estado ausente, exit y bytes diferentes; el runner devolvió exit 1, publicó `FAILED_ARTIFACTS` y conservó capturas. Esa carpeta temporal de la simulación se eliminó después de validar su ubicación segura.
- **Documentación:** `docs/vm/troubleshooting.md` contiene síntoma, módulo sospechoso, caso de reproducción y breakpoints de Visual Studio Community 2026. README y la mini-guía documentan ejecución completa y `-CaseId`.
- **Fixtures:** se mantuvieron los 81 archivos suministrados bajo `test/contracts/`; no se modificaron `.lci`, `expected_cases.json` ni `.out`, y la suite nunca los regenera.
- **Problemas conocidos:** MSBuild `FileTracker` continúa requiriendo ejecución fuera del aislamiento del agente; las compilaciones reales autorizadas finalizaron con exit 0. La sesión gráfica de Visual Studio quedó documentada pero no se afirma ejecutada.
- **Siguiente fase sugerida:** ninguna se inició. Se espera confirmación expresa del usuario antes de cualquier tarea posterior.

Esta suite comprueba ejemplos y regresiones de `LCERO_IR 1`; no depende del compilador L-Cero, no amplía la ISA y no constituye una demostración de completitud de Turing.

## Etapa 07 — Integración independiente, documentación y entrega reproducible

- **Fecha de cierre:** 21/10/2026.
- **Alcance:** auditoría final de la frontera `.lci`, documentación de arquitectura/conformidad/integración, ejemplo mínimo independiente, plan honesto de mediciones y reconstrucción limpia Debug/Release.
- **Estado:** VERIFICADO.
- **Frontera comprobada:** el proyecto contiene únicamente la VM. La CLI acepta `.lci`, el loader construye instrucciones tipadas y el intérprete no recibe `.lc`, tokens ni AST. No se agregó ningún opcode y `LABEL` continúa como directiva no ejecutable.
- **Entorno:** CMake 4.3.1-msvc1 reconoció `Visual Studio 18 2026`; MSVC 14.51.36231, Windows SDK 10.0.26100.0 y Boost.Multiprecision 1.91.0 se resolvieron correctamente con vcpkg. El toolchain se suministró mediante `VCPKG_ROOT`; no hay rutas de cuentas locales en fuentes, scripts ni documentación.
- **Reconstrucción:** se limpiaron los targets generados Debug y Release bajo `build/`, se volvió a configurar CMake y ambas configuraciones compilaron con exit 0. Se produjeron `build/bin/Debug/lcero-vm.exe` y `build/bin/Release/lcero-vm.exe`, versión 0.7.0, sin advertencias del proyecto en la salida de MSBuild.
- **CTest:** Debug pasó 6/6 en 4,98 s y Release pasó 6/6 en 4,85 s. Se conservaron las pruebas de CLI, estado, loader, intérprete, trazas y contrato ejecutado mediante el `.exe` real.
- **Contrato:** `scripts/test-contracts.ps1` pasó 40/40 en Debug y 40/40 en Release. Cada informe terminó con `HALT=15 LIMIT=3 ERROR=6 ERROR_DE_CARGA=16` y `SUITE_RESULT=PASS`; también pasaron rutas Unicode/con espacios, inmediato de 100 dígitos, límites 0/1/3 y reinicialización entre programas.
- **Smoke:** los 23 controles informados por `scripts/smoke.ps1` pasaron, incluidos ayuda, versión, argumentos inválidos, estados terminales, traza UTF-8/LF y ruta de traza inaccesible.
- **Demostraciones manuales Debug:** `examples/suma_simple.lci` produjo bytes `35-0A`, exit 0 y `HALT k=5`; `08_bucle_descendente.lci --max-steps 100000` produjo `33-0A-32-0A-31-0A`, exit 0 y `HALT k=40`; `24_salida_parcial_antes_error.lci` conservó `38-0A`, devolvió exit 2 y `ERROR` por división por cero con `k=3`.
- **Documentación:** `architecture.md` delimita módulos y estado; `compliance.md` relaciona los 18 opcodes con semántica y pruebas; `integration-guide.md` fija la entrega `.lci` del compilador; README y `guia-ejecucion.md` cubren PowerShell, Visual Studio y la solución regenerable; `measurement-plan.md` declara un protocolo futuro sin presentar resultados inventados.
- **Fixtures:** permanecen los 81 archivos suministrados bajo `test/contracts/`. Ninguna expectativa ni salida se regeneró o modificó para aprobar la suite.
- **Problemas conocidos:** MSBuild `FileTracker` necesita ejecución fuera del aislamiento del agente; con ese permiso, ambas compilaciones terminaron correctamente. No se realizó una sesión gráfica de Visual Studio, por lo que los pasos de IDE son instrucciones reproducibles y no una observación declarada.
- **Pendientes reales:** no hay un compilador L-Cero disponible en este proyecto, así que la prueba `.lc -> compilador -> .lci -> VM` sigue PENDIENTE. Tampoco se implementó profiling; el Hito III queda como plan de medición. Estas limitaciones no bloquean la entrega independiente de la VM.
- **Siguiente fase sugerida:** ninguna. La etapa queda cerrada a la espera de confirmación; no se hará push, no se creará el compilador y no se cambiará `LCERO_IR 1` sin autorización.

La evidencia empírica de los fixtures valida los comportamientos ejercitados, pero no demuestra completitud de Turing ni una cota asintótica. Del mismo modo, `LIMIT` solo informa agotamiento del presupuesto elegido.

## Validación adicional — Seis salidas reales del compilador

- **Fecha:** 22/10/2026.
- **Estado:** VERIFICADO.
- **Entrada:** seis archivos bajo `TestsParaVM/TestsParaVM`: tres programas normales y sus tres versiones optimizadas. Se conservaron sin modificaciones.
- **Validez formal:** los seis pasaron `--check`, por lo que satisfacen la gramática superficial de `LCERO_IR 1`, los tipos y aridades de operandos, las cotas `MEM` y la resolución de etiquetas. Esta aceptación estática no fue confundida con ejecución.
- **Ejecución Debug:** 6/6 alcanzaron `HALT`, exit 0 y `stdout` exacto con LF. Los pasos fueron 79/35 para aritmética normal/optimizada, 76/34 para condicionales y 708/318 para bucles anidados.
- **Ejecución Release:** 6/6 reprodujeron exactamente los mismos estados, pasos y bytes que Debug.
- **Resultados:** aritmética produjo `38, 3, -3, 26`; condicionales `100, 400, 600, 700`; bucles anidados `60, 40, 5, 4`.
- **Equivalencia observada:** cada pareja normal/optimizada produjo la misma secuencia observable `O` pese a tener distinto `P`, `MEM`, `pc` terminal y `k`. Esto verifica esos casos concretos, no demuestra la corrección general del optimizador.
- **Automatización:** se agregó `scripts/test-compiler-outputs.ps1`, con `--check`, presupuesto de 100000 pasos, timeout externo, comparación byte a byte y capturas separadas. `docs/vm/pruebas-del-compilador.md` explica su uso y cómo probar otros `.lci`.
- **Regresión:** CTest se repitió secuencialmente y pasó 6/6 en Debug (2,15 s) y 6/6 en Release (2,21 s). Un intento inicial de ejecutar ambas configuraciones en paralelo produjo una colisión entre nombres temporales de `lcero_vm_cli`; al repetir cada configuración por separado, como indican los comandos documentados, ambas pasaron. No fue un fallo semántico de la VM.
- **Cambios en la VM:** ninguno. No fue necesario relajar el loader, cambiar la ISA ni adaptar resultados esperados.
- **Límite de la evidencia:** no se recibió ni ejecutó el compilador ni los `.lc` originales. Se comprobó la compatibilidad de sus seis artefactos `.lci`, no el flujo extremo a extremo completo.

## Portabilidad adicional — Resolución de `.lci` desde otra carpeta

- **Fecha:** 22/10/2026.
- **Estado:** VERIFICADO.
- **Necesidad:** el repositorio se trasladó a otra carpeta y debía localizar `TestsParaVM` aunque el CWD de PowerShell o Visual Studio fuese distinto.
- **Convención:** una ruta absoluta no cambia. Una ruta relativa conserva primero la resolución histórica respecto del CWD; si no existe allí, se prueba la misma ruta ascendiendo desde la ubicación real de `lcero-vm.exe`, obtenida con `GetModuleFileNameW`.
- **Frontera formal:** la búsqueda física ocurre antes del loader. No agrega tokens, directivas u opcodes y no cambia `LCERO_IR 1`; el loader continúa recibiendo un solo archivo `.lci`.
- **Prueba unitaria:** `lcero_vm_cli` crea una RI fuera del CWD y simula el ejecutable bajo `build/bin/Debug`; la ruta `TestsParaVM/portabilidad.lci` alcanza `HALT` y produce `17` seguido por LF.
- **Prueba manual externa:** desde la carpeta anterior se comprobó que `TestsParaVM\prueba_1.lci` no existía respecto de ese CWD. El `.exe` del repositorio nuevo resolvió el archivo, `--check` devolvió exit 0 y la ejecución produjo `38, 3, -3, 26`, `HALT`, `k=79` y exit 0.
- **Suite:** CTest pasó 6/6 en Debug y 6/6 en Release. El runner invocado desde la carpeta anterior encontró automáticamente el proyecto nuevo y pasó 6/6 salidas del compilador.

## Auditoría de portabilidad y guías para usuarios

- **Fecha:** 22/10/2026.
- **Estado:** VERIFICADO.
- **Alcance de auditoría:** fuentes C++, cabeceras, CMake, presets, scripts, pruebas, fixtures y documentación distribuible. Se excluyeron `.git` y el directorio generado `build`.
- **Resultado de rutas:** cero coincidencias de perfiles de Windows, nombres del equipo de desarrollo o rutas Windows absolutas codificadas. Las rutas se obtienen con `${sourceDir}`, variables CMake, `VCPKG_ROOT`, `$PSScriptRoot`, `Resolve-Path`, `Join-Path` y la ubicación real del ejecutable.
- **Resultado de privacidad:** cero valores coincidentes con patrones de contraseñas, tokens de acceso o claves de GitHub. `.gitignore` mantiene fuera del producto configuraciones de agentes, archivos privados, secretos y artefactos locales.
- **Documentación:** se agregó `guias/` con instalación, primer programa, suites, trazas, diagnóstico, integración con el compilador y portabilidad/privacidad. Los ejemplos de rutas fijas se reemplazaron por detección con `vswhere` y resolución local.
- **Compilación:** Debug y Release completaron correctamente con MSBuild; se generaron ambos `lcero-vm.exe` solo para la verificación local.
- **Regresión:** CTest pasó 6/6 en Debug y 6/6 en Release. Los seis archivos de `TestsParaVM` pasaron 6/6 en ambas configuraciones con salida LF exacta.
- **Prueba desde otro CWD:** desde el directorio temporal, donde `TestsParaVM\prueba_1.lci` no existía, `--check` devolvió 0 y la ejecución alcanzó `HALT`, `k=79`, salida `38, 3, -3, 26` y exit 0.
- **Entrega:** `build/` es regenerable, está ignorado y se elimina después de verificar porque los cachés de CMake incorporan necesariamente rutas absolutas del equipo que compila.
