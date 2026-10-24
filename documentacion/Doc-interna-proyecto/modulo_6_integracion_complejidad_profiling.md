# L-Cero — Módulo 6: integración, complejidad, profiling y cierre

Fecha de verificación: **24 de octubre de 2026**  
Revisión Git inspeccionada: `6621c7830c355a89fbad6af526ba29b1bccfd667` más los cambios de trabajo enumerados en este informe.

## 1. Introducción y alcance

Este módulo verifica el compilador L-Cero contra la máquina virtual independiente,
sin enlazar ambos proyectos y sin cambiar `LCERO_IR 1`. Incluye pruebas manuales
de la ISA, pruebas extremo a extremo para los dos backends, casos negativos,
instrumentación silenciosa, profiling repetible, análisis asintótico y trazabilidad
académica.

La documentación de alcance general se ubica en `documentacion/`, en la raíz,
por indicación expresa del responsable del repositorio. Los artefactos producidos
por el compilador continúan bajo `Compilador/integration/`.

Se inspeccionaron el Documento Maestro, el contrato aprobado, los documentos y
el código de los módulos 0 a 5, la documentación y el código de la VM, las suites
existentes y la especificación del Módulo 6. No se encontró dentro del workspace
un archivo separado identificable como «consigna institucional original»; por
eso la trazabilidad académica usa los requisitos consolidados del Documento
Maestro y del Módulo 6 y deja ese cotejo externo explícitamente pendiente.

## 2. Identificación de los proyectos

| Componente | Raíz | Solución Visual Studio | Ejecutable Release x64 |
|---|---|---|---|
| Compilador | `Compilador/` | `Compilador/LCero-Compilador.slnx` | `Compilador/bin/x64/Release/LCero.Compiler.exe` |
| Máquina virtual | `VM/` | `VM/LCero-VM.slnx` | `VM/bin/x64/Release/lcero-vm.exe` |

La detección automática no depende de los nombres de las carpetas: busca los
marcadores de solución y proyecto en los hijos de la raíz y exige exactamente
un candidato de cada tipo. La lógica está en
`Compilador/integration/scripts/Common.ps1`.

Los proyectos se compilan y ejecutan de manera independiente. No se agregaron
referencias, enlaces de bibliotecas ni dependencias de MSBuild entre ellos. En
este módulo la VM se trató como componente externo de sólo lectura; únicamente
se escribieron productos ignorados de compilación y una carpeta temporal de
transferencia, luego eliminada.

Nota de auditoría: al comenzar el Módulo 6 ya existían cambios sin confirmar en
archivos de configuración/proyecto de la VM, derivados de la solicitud anterior
de disponer de una solución nativa de Visual Studio. No pertenecen a este módulo.
Los scripts toman como línea base el contenido existente al empezar cada corrida
y comprueban por SHA-256 que ningún archivo protegido de la VM cambie durante
integración o profiling.

## 3. Arquitectura de integración

El límite entre procesos es exclusivamente el archivo textual `.lci`:

```text
archivo .lc
    │
    ▼
LCero.Compiler.exe --backend reference|optimized -o artefacto.lci
    │  escritura en Compilador/integration/artifacts/
    ▼
Copy-Item + SHA-256 de origen y copia
    │  copia física a VM/.module6-transfer/
    ▼
lcero-vm.exe --check copia.lci
    │
    ▼
lcero-vm.exe --max-steps N copia.lci
    │
    └── stdout, stderr, código, LCERO_STATUS y LCERO_STEPS
```

No se usan enlaces simbólicos o duros. Una compilación fallida no puede reutilizar
un `.lci` previo: el compilador invalida primero el destino y el script sólo copia
después de una salida exitosa. El hash SHA-256 debe coincidir antes de invocar la
VM. La copia se elimina en un bloque `finally` y el mapa de hashes protegidos de
la VM se compara antes y después.

## 4. Contrato compartido `LCERO_IR 1`

No se modificó el contrato. El compilador emite y valida el mismo formato que el
cargador de la VM consume: encabezado `LCERO_IR 1`, `MEM n`, cuatro registros,
enteros exactos, etiquetas y las instrucciones de transferencia, aritmética,
comparación, salto, salida y terminación.

La matriz completa y reutilizable está en
`documentacion/tabla_compatibilidad_isa.csv`. Resumen:

| Familia | Firmas verificadas | Evidencia observada | Estado |
|---|---|---|---|
| Formato | `LCERO_IR 1`, `MEM n`, incluido `MEM 0` | cargador y `--check` | PASS |
| Datos | `CONST`, `LOAD`, `STORE`, `MOV` | casos manuales 01, 03, 04, 06 y 07 | PASS |
| Aritmética | `ADD`, `SUB`, `MUL`, `DIV` | caso manual 01; división por cero 05 | PASS |
| Comparación | los seis `CMP_*` | caso manual 02; producen 0/1 | PASS |
| Control | `JMP`, `JZ`, `LABEL` | casos manuales 03 y 08 | PASS |
| E/S y fin | `PRINT`, `HALT` | todos los casos normales | PASS |
| Recursos | `--max-steps` | caso manual 08 finaliza `LIMIT` en 4 pasos | PASS |

El comportamiento se localizó en `VM/src/vm/interpreter.cpp`; la sintaxis,
etiquetas, rangos y operandos se validan en el cargador de `VM/src/loader/`.
La división usa `boost::multiprecision::cpp_int` y trunca hacia cero. Las
comparaciones escriben exactamente `0` o `1`. Una lectura sin inicializar y la
división por cero terminan con `ERROR`; una dirección inválida es rechazada por
el cargador. `LIMIT` sólo significa que se agotó el presupuesto observado.

No se detectaron incompatibilidades contractuales ni defectos de la VM.

## 5. Pruebas manuales de ISA

Los ocho archivos de `Compilador/integration/manual-lci/` se escribieron sin
intervención del compilador para separar fallos de ISA de fallos de traducción.

| Caso | Propósito | `--check` | Estado/exit | Pasos | Resultado |
|---|---|---:|---|---:|---|
| `01_aritmetica_mov` | `CONST`, `MOV`, cuatro operaciones, `PRINT`, `HALT` | 0 | HALT / 0 | 12 | PASS |
| `02_comparaciones` | los seis `CMP_*` | 0 | HALT / 0 | 15 | PASS |
| `03_memoria_control` | `LOAD`, `STORE`, `JMP`, `JZ`, `LABEL` | 0 | HALT / 0 | 11 | PASS |
| `04_entero_grande` | entero mayor que 64 bits | 0 | HALT / 0 | 5 | PASS |
| `05_division_por_cero` | error dinámico | 0 | ERROR / 2 | 2 | PASS |
| `06_registro_no_inicializado` | lectura inválida | 0 | ERROR / 2 | 0 | PASS |
| `07_direccion_invalida` | rango de memoria inválido | 3 | ERROR_DE_CARGA / 3 | 0 | PASS |
| `08_limite_pasos` | bucle bajo presupuesto | 0 | LIMIT / 4 | 4 | PASS |

Los hashes del original y la copia están preservados en
`Compilador/integration/results/manual_isa_results.csv`; las ocho comparaciones
fueron `PASS`.

## 6. Pruebas extremo a extremo

### 6.1 Cobertura

Se ejecutaron doce fuentes válidos, con error dinámico o no terminantes con
ambos backends:

1. constantes, cero, positivos, negativos, varios `print` y literal grande;
2. aritmética, precedencia, asociatividad, paréntesis, menos unario y operandos repetidos;
3. división exacta, con resto y con signos, truncada hacia cero;
4. declaración, asignación, reasignación y combinación de variables;
5. seis comparaciones;
6. `if` verdadero/falso, `else` y anidamiento;
7. bucles cero/varias iteraciones y anidados;
8. presión de registros y spilling;
9. multiplicación exacta con valores mayores que 64 bits;
10. división por cero;
11. salida observable anterior a un error dinámico;
12. bucle no terminante con invariante `x = 0`, limitado a 64 pasos.

Además se compilaron cinco entradas inválidas por backend: error léxico, error
sintáctico, variable no declarada, declaración duplicada y variable posiblemente
no inicializada. Se verificó el rechazo y la ausencia de un artefacto nuevo.

### 6.2 Resultado

| Clase | Filas ejecutadas | Resultado |
|---|---:|---|
| Terminación normal | 18 | 18 PASS |
| Error dinámico esperado | 4 | 4 PASS |
| Límite de pasos esperado | 2 | 2 PASS |
| Rechazo del compilador | 10 | 10 PASS |
| **Total** | **34** | **34 PASS** |

Para las nueve entradas normales, los backends coincidieron con el oráculo
esperado, no sólo entre sí. En los errores dinámicos coincidieron estado,
categoría y salida parcial esperada. No se exigió igual número de pasos: el
backend optimizado precisamente puede emitir menos instrucciones.

El caso `12_bucle_no_terminante.lc` se compiló con ambos backends y atravesó la
misma copia física con verificación SHA-256. En los dos recorridos la VM devolvió
exit 4, `LIMIT`, 64 pasos y stdout vacío. El invariante del programa fundamenta
su no terminación; `LIMIT` sólo certifica el agotamiento del presupuesto.

Los detalles, estados, códigos, pasos y hashes se conservan en
`Compilador/integration/results/integration_results.csv`. Los `.lci` realmente
ejecutados están en `Compilador/integration/artifacts/lci/`.

## 7. Suites heredadas y automatización

La corrida integral ejecutó:

- suite del compilador: **71/71**;
- cinco ejecutables de prueba de la VM: modelo, CLI, cargador, intérprete y trazas;
- suite contractual externa de la VM: **40 casos**;
- ocho casos manuales nuevos;
- treinta y dos filas de integración nuevas.

La suite del compilador contenía 70 pruebas antes del módulo. Se agregó una
prueba que comprueba que la instrumentación temporal sea estructurada, no nula y
silenciosa en la interfaz cotidiana. Los casos de integración no se mezclaron
con el contador de la suite unitaria.

Comandos de reproducción desde la raíz:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Compilador\integration\scripts\Run-Module6Integration.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File .\Compilador\integration\scripts\Run-Profiling.ps1 -Warmups 2 -Repetitions 7
powershell -NoProfile -ExecutionPolicy Bypass -File .\Compilador\integration\scripts\Generate-Charts.ps1
```

`Run-Module6Integration.ps1` puede recibir `-SkipBuild` si ambas soluciones ya
fueron construidas. Todo fallo produce código distinto de cero.

## 8. Incidencias y correcciones

### 8.1 Defecto reproducido

La primera regresión dio **69/70**, tanto en Debug como en Release. La prueba de
artefactos de gramática comparaba texto generado con archivos CSV byte a byte.
Los archivos del checkout tenían CRLF y el generador de prueba producía LF. El
contenido lógico era idéntico; el problema estaba en la prueba y era dependiente
de plataforma.

### 8.2 Corrección mínima

Se añadió normalización local de finales de línea en `Compilador/tests/test_main.cpp`
antes de comparar esos artefactos. No se modificaron la gramática, el parser, el
contrato ni los CSV. Después de la corrección, Debug y Release aprobaron 71/71.

### 8.3 Instrumentación

`CompilationResult` recibió tiempos por fase en nanosegundos y `Compiler::run`
los completa con reloj monótono. Sólo el modo interno `--profile-case` del
ejecutable de pruebas serializa esas métricas a CSV. El ejecutable cotidiano, el
menú y sus mensajes no imprimen tiempos, grafos, temporales ni spills.

No se encontraron defectos semánticos del compilador, defectos de la VM ni
ambigüedades de `LCERO_IR 1` durante las pruebas.

## 9. Estado de compilación

| Proyecto | Configuración | Resultado |
|---|---|---|
| Compilador | Debug x64 | correcto; 0 errores y 0 advertencias |
| Compilador | Release x64 | correcto |
| VM | Release x64 | correcto |

Herramientas observadas: Visual Studio Community 18, MSBuild
`18.10.1.42706`, MSVC `19.51.36260`, C++17, plataforma x64.

## 10. Metodología de profiling

### 10.1 Entorno

- Sistema: Windows NT `10.0.26200.0`.
- Procesador informado por el entorno: AMD64 Family 23 Model 160, 8 procesadores lógicos.
- Reloj: `System.Diagnostics.Stopwatch`, frecuencia observada de 10 MHz.
- Binarios: Release x64.
- Calentamientos: 2 por combinación.
- Repeticiones registradas: 7 por combinación.
- Estadístico comparativo: mediana.
- Revisión medida: la indicada al comienzo, con los cambios de trabajo del módulo.

No fue posible consultar mediante CIM el modelo comercial del procesador ni la
RAM porque el entorno denegó esa consulta; no se inventan esos valores.

### 10.2 Familias

`Generate-ProfilingCases.ps1` construye dieciséis fuentes válidos:

| Familia | Escalas | Aspecto principal |
|---|---|---|
| `secuencia` | 10, 40, 80, 160 | secuencias aritméticas y tamaño de RI |
| `variables` | 10, 25, 50, 100 | tabla de símbolos y slots |
| `presion` | 4, 8, 16, 32 | árboles balanceados y temporales simultáneamente vivos |
| `control` | 5, 10, 20, 40 | condiciones y bucles repetidos |

Cada fuente se mide con `reference` y `optimized`. Son 32 combinaciones × 7
repeticiones = **224 compilaciones registradas**. La misma cantidad de ejecuciones
de VM se registra después de una copia física verificada. Los calentamientos no
se incluyen en los CSV crudos.

El tiempo del compilador excluye MSBuild y el inicio del ejecutable de profiling:
se mide dentro de `Compiler::run`. El tiempo de VM sí es externo y por ello
incluye creación del proceso, carga, parsing y salida.

### 10.3 Datos disponibles y limitaciones

Se registran fuente, `.lci`, tiempo total y por fases, tokens, AST resumido, RI,
temporales, bloques, aristas, instrucciones físicas, `LOAD`, `STORE`, `MEM`,
spills, iteraciones de vivacidad, estado, pasos dinámicos y tiempo externo de VM.

La VM expone pasos dinámicos pero no separa accesos dinámicos a memoria. Por eso
`LOAD + STORE` es una cuenta **estática** del archivo y no se presenta como
acceso dinámico. Los tiempos de programas pequeños están dominados en buena parte
por el arranque del proceso; no permiten aislar la velocidad del intérprete.

## 11. Resultados experimentales

### 11.1 Resumen representativo

| Caso | Backend | Compilación mediana (ms) | Instr. físicas | `MEM` | `LOAD+STORE` | Spills |
|---|---|---:|---:|---:|---:|---:|
| secuencia 10 | reference | 6.588 | 109 | 33 | 86 | 0 |
| secuencia 10 | optimized | 7.047 | 45 | 1 | 22 | 0 |
| secuencia 40 | reference | 7.265 | 409 | 123 | 326 | 0 |
| secuencia 40 | optimized | 7.986 | 165 | 1 | 82 | 0 |
| secuencia 80 | reference | 10.205 | 809 | 243 | 646 | 0 |
| secuencia 80 | optimized | 7.483 | 325 | 1 | 162 | 0 |
| secuencia 160 | reference | 9.005 | 1609 | 483 | 1286 | 0 |
| secuencia 160 | optimized | 9.183 | 645 | 1 | 322 | 0 |
| variables 100 | reference | 12.470 | 999 | 399 | 798 | 0 |
| variables 100 | optimized | 12.106 | 401 | 100 | 200 | 0 |
| presión 32 | reference | 6.695 | 191 | 63 | 126 | 0 |
| presión 32 | optimized | 7.484 | 95 | 15 | 30 | 15 |
| control 40 | reference | 8.062 | 809 | 243 | 606 | 0 |
| control 40 | optimized | 10.388 | 325 | 1 | 122 | 0 |

Sobre las dieciséis parejas:

- reducción media de instrucciones físicas estáticas: **57,33 %**;
- reducción media de `LOAD + STORE` estáticos: **77,22 %**;
- diferencia media relativa de tiempo de compilación optimized/reference:
  **+1,12 %**;
- diferencia media relativa del tiempo externo de VM: **−1,81 %**.

Los dos porcentajes temporales no constituyen una ganancia general demostrada:
hay casos mejores y peores y el ruido fijo es grande frente a tiempos de 6–12 ms
de compilación y aproximadamente 33–42 ms de proceso VM.

### 11.2 Spilling observado

| Escala de presión | Temporales | Aristas | Spills | `MEM` optimizado | `LOAD+STORE` |
|---:|---:|---:|---:|---:|---:|
| 4 | 7 | 5 | 1 | 1 | 2 |
| 8 | 15 | 17 | 3 | 3 | 6 |
| 16 | 31 | 49 | 7 | 7 | 14 |
| 32 | 63 | 129 | 15 | 15 | 30 |

La progresión confirma que el caso generador aumenta la presión y que cada
temporal derramado recibe un slot y materializaciones observables. No prueba por
sí misma una ley asintótica universal.

### 11.3 Interpretación empírica

En `secuencia`, las instrucciones crecen aproximadamente de forma proporcional
a la escala para ambos backends, con una constante menor en el optimizado. Las
familias finitas son coherentes con el análisis de recorridos lineales en fases
front-end y emisión, pero no demuestran una cota Θ.

La familia `presion` incrementa simultáneamente temporales, aristas y spills; es
la que mejor expone el trabajo adicional del grafo. Los tiempos medidos son muy
pequeños y variables para ajustar una curva fiable. Se usa el gráfico para mostrar
tendencia, no para inferir un exponente exacto.

## 12. Gráficos reproducibles

Los seis SVG se generan desde `profiling_summary.csv`:

1. `Compilador/integration/graphs/01_tiempo_compilacion.svg`;
2. `02_instrucciones_fisicas.svg`;
3. `03_accesos_memoria_estaticos.svg`;
4. `04_memoria_declarada.svg`;
5. `05_tiempo_vm.svg`;
6. `06_costo_optimizador.svg`.

Contienen título, ejes, unidad, leyenda y procedencia. El script es
`Compilador/integration/scripts/Generate-Charts.ps1` y no requiere paquetes
gráficos externos.

## 13. Parámetros del análisis de complejidad

Se usan parámetros distintos porque no todas las fases dependen de una sola
dimensión:

- `n`: bytes/caracteres del fuente;
- `t`: tokens producidos, incluido el fin de archivo cuando corresponda;
- `A`: nodos del AST;
- `V`: variables/símbolos;
- `D`: cantidad total de dígitos decimales de literales;
- `N`: instrucciones de RI;
- `Q`: caracteres del `.lci` emitido;
- `T`: temporales virtuales;
- `B`: bloques básicos;
- `E`: aristas del CFG;
- `G_E`: aristas no dirigidas del grafo de interferencia;
- `P`: pasadas hasta el punto fijo de vivacidad;
- `L_i`: cantidad de valores vivos en el punto de programa `i`;
- `K`: registros asignables; en el backend actual, `K = 2`.

`O(f)` es una cota superior, `Ω(f)` una inferior y `Θ(f)` una cota ajustada.
Sólo se escribe Θ donde las dos cotas están justificadas por la implementación.
No se postula un caso promedio: no existe un modelo probabilístico de fuentes.

## 14. Complejidad teórica del compilador

| Fase | Tiempo | Espacio adicional | Fundamentación |
|---|---|---|---|
| Lectura | Θ(n) | Θ(n) | copia todos los bytes válidos |
| Lexer con AFD fijo | Θ(n) | O(n + t) | una transición constante por byte y materialización de lexemas cuyo tamaño total no supera el fuente |
| Parser LL(1) + AST | Θ(t) | O(A + n + h) | una producción/consumo acotado por token; `h` es altura recursiva y los nombres/literales conservan texto del fuente |
| Semántica | O(A V log V + C) | O(A V + h + C_s) | mapas/conjuntos ordenados, comprobación de `I ⊆ D` en fronteras y registros de transferencia que conservan conjuntos |
| Generación de RI | O(A log V + D) | O(N + T + V) | visita AST, consulta símbolos y conserva literales |
| Backend reference | Θ(N + Q) | O(V + T + Q) | baja cada instrucción con cargas/almacenamientos explícitos |
| CFG | O(N log(N + L) + E) | O(N + B + E) | `std::map` de etiquetas, `std::set` de líderes y recorridos |
| Vivacidad | O(P(B + E)T + NT) con conjuntos abstractos; factores logarítmicos por `std::set` | O(NT + BT) | ecuaciones monotónicas hasta punto fijo y pasada inversa |
| Interferencia | O(Σᵢ L_i² log T + NT log T), peor O(NT² log T) | O(T + G_E) | crea cliques `live_in`, `live_out`, usos y aristas definición/salida |
| Chaitin heurístico | O(T² + G_E log T) | O(T + G_E) | cada eliminación recorre activos; borra aristas ordenadas; selección recorre vecindad |
| Emisión con spills | O(N + S + Q) | O(V + S + Q) | materializa cargas/almacenamientos; `S` es número de operaciones de spill emitidas |
| Backend optimized | suma CFG + vivacidad + interferencia + Chaitin + emisión | O(NT + BT + T + G_E + Q) | ejecuta esas fases en secuencia |
| Validador LCI | O(Q + (L+J) log L) | O(Q + L + J) | tokeniza caracteres; etiquetas en `std::set`; `J` referencias |
| Publicación | Θ(Q) | O(Q) | temporal, verificación y renombrado transaccional |

`C` y `C_s` representan respectivamente el trabajo y espacio transitorio de
copiar e intersectar conjuntos de inicialización en las estructuras de control;
en peor caso quedan acotados por el número de puntos de control multiplicado por
`V` (con factores logarítmicos para inserciones ordenadas). Los registros de
transferencia persistentes explican por separado el término `O(A V)` de espacio.
La tabla evita esconder estos costos bajo una afirmación lineal falsa.

### 14.1 Argumentos principales

El compilador completo tiene cota inferior `Ω(n)`: debe distinguir fuentes que
difieren en su último byte. Lectura y lexer efectivamente examinan cada byte un
número constante de veces, de donde sale Θ(n) con alfabeto y AFD fijos. El parser
predictivo no hace backtracking; cada paso consume token o aplica una producción
epsilon acotada por la gramática, por eso es Θ(t).

Vivacidad opera sobre el retículo finito de subconjuntos de `T`. Sus ecuaciones
son monotónicas y cada bit sólo puede pasar de ausente a presente; termina. La
cota se expresa con `P` porque la implementación itera bloques completos y el
número observado depende del CFG. La representación mediante `std::set` añade
costos de mezcla/inserción que la notación abstracta de bits no mostraría.

Interferencia llama `add_clique` para conjuntos vivos: un conjunto de tamaño
`L_i` induce hasta `L_i(L_i-1)/2` intentos de arista. Sumando todos los puntos se
obtiene la expresión cuadrática local y el peor caso `O(NT² log T)`.

Chaitin no resuelve coloreo óptimo. En cada una de `T` eliminaciones puede recorrer
el conjunto activo, dando `O(T²)`; eliminar vecindades suma el trabajo sobre
aristas, y los `std::set::erase` agregan el factor logarítmico. La recoloración
recorre nuevamente las aristas. Es una cota del algoritmo implementado, no una
afirmación de optimalidad.

Los literales arbitrariamente grandes invalidan el supuesto de palabra constante:
el compilador al menos copia/valida `D` dígitos. En la VM, sumar, multiplicar o
dividir `b` bits tampoco es O(1); su costo depende del algoritmo multiprecisión.
Ese costo dinámico no forma parte de la complejidad de compilar un `while`, y la
cantidad de iteraciones de un `while` no se incluye en `N`.

## 15. Fundamentación académica complementaria

### 15.1 Semántica y autómatas linealmente acotados

Bajo una codificación finita, cada identificador ocupa parte del propio fuente y
hay como máximo O(n) símbolos distintos. Las verificaciones de declaración,
igualdad de nombres e inicialización definida pueden simularse releyendo y
marcando una cinta de O(n) celdas. Esto da un argumento de pertenencia a espacio
lineal para el conjunto de programas sintáctica y semánticamente válidos y, por
la caracterización correspondiente, permite reconocerlo con un LBA.

El argumento no prueba que el lenguaje no sea libre de contexto ni que un LBA
sea el modelo mínimo necesario. Por eso la relación CSL/LBA figura como parcial
en la matriz académica.

### 15.2 Capacidad computacional ideal

Se usa una máquina de Minsky de dos contadores, modelo universal conocido. Para
cada máquina finita `M` se construye efectivamente un programa L-Cero `F(M,w)`
con variables `c1`, `c2`, `pc` y `old`. Los contadores comienzan con la codificación
de la entrada y `pc` con el estado inicial:

```text
while (pc != HALT_STATE) {
    old = pc;
    if (old == q_inc_1) { c1 = c1 + 1; pc = destino; }
    if (old == q_dec_2) {
        if (c2 == 0) { pc = destino_cero; }
        else { c2 = c2 - 1; pc = destino_no_cero; }
    }
    // una rama por cada instrucción finita de M
}
```

Los guardas consultan `old`, que no cambia dentro de la iteración; por eso una
actualización de `pc` no dispara una segunda instrucción en la misma vuelta. Por
inducción en el número de pasos, después de cada iteración `(pc,c1,c2)` coincide
con la configuración siguiente de `M`. El decremento sólo ocurre cuando el
contador no es cero, así se conserva el invariante natural. El programa sale si
y sólo si la máquina entra al estado de parada.

Esto justifica universalidad del **modelo ideal** con enteros y memoria no
acotados. La VM física tiene memoria, tiempo y presupuestos finitos; ejecutar
muchos ejemplos no es la demostración y obtener `LIMIT` no decide divergencia.

La traducción de las construcciones del fuente a RI está argumentada por inducción
estructural en `Compilador/docs/modulo_4_representacion_intermedia.md`, y las
pruebas de este módulo aportan evidencia de las dos bajadas a `LCERO_IR 1`. No hay
una prueba mecanizada integral de preservación semántica de todas las capas; ésa
es la razón del estado «parcial» en esa fila específica de trazabilidad.

### 15.3 Problema de parada

Supóngase un decisor `H(P,x)` para la parada de programas L-Cero ideales. Dada
una máquina de dos contadores `M` y entrada `w`, se calcula `F(M,w)` con la
construcción anterior y se consulta `H(F(M,w), vacío)`. Por la equivalencia
inductiva, respondería si `M` se detiene sobre `w`, contradiciendo la
indecidibilidad de la parada para ese modelo universal. Luego no existe tal
decisor para L-Cero ideal. `--max-steps` sólo reconoce «no terminó antes de k».

### 15.4 K-coloreo y asignación de registros

Dado un grafo de interferencia general y `K`, una asignación sin spills es
exactamente un K-coloreo: temporales adyacentes deben tener colores distintos.
Una coloración propuesta se verifica en `O(T + G_E)`, así el problema pertenece
a NP; para `K ≥ 3`, la reducción identidad desde K-COLOR muestra NP-dureza y el
problema general es NP-completo.

El componente acepta un `InterferenceGraph` explícito, por lo que esa relación es
válida para su problema de entrada general. No se afirma, sin una reducción
adicional, que la subfamilia de grafos que pueden surgir de programas L-Cero sea
NP-dura. El asignador implementado usa simplificación, heurística de spill y
selección determinista; produce una solución válida, no necesariamente óptima.

## 16. Matriz de cumplimiento

La matriz detallada está en `documentacion/matriz_trazabilidad.csv`. Totales
cualitativos:

- Hito I: autómatas, gramática, parser, AST y semántica cumplidos; relación
  CSL/LBA documentada con alcance parcial prudente.
- Hito II: RI, contrato y VM independiente cumplidos; simulación de dos
  contadores y reducción de parada documentadas; formalización mecanizada
  extremo a extremo no realizada.
- Hito III: análisis asintótico, profiling, gráficos, CFG, vivacidad,
  interferencia, Chaitin, spilling y comparación de backends cumplidos.
- Cotejo contra una consigna institucional separada: pendiente porque ese
  documento no estaba disponible como artefacto independiente.

## 17. Limitaciones conocidas

1. Los resultados experimentales corresponden a una máquina y una sesión; no
   son garantías universales de rendimiento.
2. El tiempo externo de VM incluye inicio del proceso y no aísla interpretación.
3. No existe contador externo de accesos dinámicos a memoria; sólo cuenta estática.
4. Los generadores cubren familias representativas, no todos los programas.
5. `LIMIT` no demuestra ejecución infinita.
6. La universalidad se refiere al modelo ideal; los recursos físicos son finitos.
7. No hay prueba mecanizada completa de corrección del compilador.
8. Falta el cotejo documental independiente con la consigna institucional original.

## 18. Conclusiones y estado final

La compatibilidad real quedó comprobada mediante archivos copiados físicamente,
no por una integración en memoria. Ambos backends produjeron el comportamiento
esperado en los casos normales y dinámicos; el optimizado redujo sustancialmente
instrucciones y operaciones estáticas de memoria en estas familias. Se conservaron
el backend de referencia, el menú interactivo y el contrato.

El compilador está técnicamente en condiciones de presentarse como **candidato a
v1.0**: compila, mantiene 71/71 pruebas, pasa 34/34 filas de integración y posee
mediciones reproducibles. No se crea commit ni tag. Para declarar el cierre
académico definitivo conviene que un responsable coteje la matriz con la consigna
institucional original y acepte el alcance explícito de las demostraciones
formales parciales.

---

# AUDITORÍA DEL MÓDULO 6 — INFORMACIÓN PARA REVISIÓN EXTERNA

## A. Identificación de proyectos

- Compilador: `Compilador/LCero-Compilador.slnx`; binario `LCero.Compiler.exe`.
- VM: `VM/LCero-VM.slnx`; binario `lcero-vm.exe`.
- Ambos se construyen por separado y no se referencian.
- Las rutas se detectan por contenido en `Common.ps1`, no se presuponen.

## B. Contrato

- Verificadas todas las firmas de `LCERO_IR 1`, `MEM 0`, registros, enteros
  grandes, división, comparaciones, saltos, salida, terminación y errores.
- Matriz: `documentacion/tabla_compatibilidad_isa.csv`.
- Incompatibilidades: ninguna observada.
- Cambio contractual: ninguno.

## C. Transferencia de archivos

- `Copy-Item` a `VM/.module6-transfer/`.
- SHA-256 del artefacto y de la copia antes de `--check` y ejecución.
- La compilación debe terminar correctamente; el destino del compilador se
  invalida de antemano para impedir artefactos viejos.
- Las copias temporales se eliminan; no se enlazan los proyectos.

## D. Integración

- 12 programas × 2 backends y 5 rechazos × 2 backends: 34 filas.
- 18 HALT, 4 ERROR dinámico, 2 LIMIT y 10 rechazos: 34 PASS.
- Estados, pasos, códigos, stdout y hashes: `integration_results.csv`.
- Ocho casos manuales adicionales cubren ISA, errores y LIMIT: 8 PASS.

## E. Incidencias

- Error encontrado: comparación CRLF/LF en una prueba de documentación generada.
- Responsabilidad: prueba del compilador.
- Corrección: normalización local de fin de línea.
- Regresión final: 71/71.
- VM: ningún defecto reproducido.

## F. Complejidad

- Parámetros y cotas están en las secciones 13 y 14.
- Θ sólo se usa para lectura, lexer/parser bajo sus supuestos y publicación.
- Grafos y conjuntos conservan parámetros `T`, `B`, `E`, `G_E`, `P` y `L_i`.
- Teoría y observación experimental se presentan por separado.

## G. Profiling

- Release x64, 2 calentamientos, 7 repeticiones y mediana.
- 224 compilaciones y 224 ejecuciones registradas.
- Crudos: `compiler_runs.csv`, `vm_runs.csv`.
- Resumen: `profiling_summary.csv`.
- Reducciones medias observadas: 57,33 % de instrucciones y 77,22 % de
  `LOAD+STORE` estáticos.

## H. Gráficos

- Seis SVG enumerados en la sección 12.
- Origen único: CSV medidos.
- Regeneración: `Generate-Charts.ps1`.
- Resultado principal: menor código y memoria estática; tiempos demasiado
  pequeños/ruidosos para garantizar aceleración general.

## I. Verificación académica

- Matriz de los tres hitos: `documentacion/matriz_trazabilidad.csv`.
- Demostrados/argumentados: AFD, LL(1), RI, análisis, simulación ideal de dos
  contadores, reducción de parada, cotas y relación general con K-coloreo.
- Pendientes explícitos: cotejo con consigna original separada, prueba mecanizada
  extremo a extremo y clasificación mínima CSL/LBA.

## J. Compilación y pruebas

```powershell
MSBuild Compilador\LCero-Compilador.slnx /t:Build /m /p:Configuration=Debug /p:Platform=x64
MSBuild Compilador\LCero-Compilador.slnx /t:Build /m /p:Configuration=Release /p:Platform=x64
MSBuild VM\LCero-VM.slnx /t:Build /m /p:Configuration=Release /p:Platform=x64
```

- Compilador: Debug y Release correctos; Debug observado con 0 errores/advertencias.
- VM: Release correcto.
- Heredadas: 70; suite final: 71/71.
- VM: cinco suites nativas y 40 casos contractuales ejecutados.
- Nuevas: 8 manuales y 34 filas de integración, todas PASS.

## K. Estado final

- Pipeline completo y dos backends funcionales.
- Publicación transaccional, validación y CLI independiente conservados.
- VM no modificada por el Módulo 6; hashes protegidos iguales antes/después.
- Compilador y VM siguen pudiendo utilizarse por separado.
- Recomendación: candidato técnico a v1.0; realizar el cotejo académico pendiente
  antes de crear manualmente el tag definitivo.
