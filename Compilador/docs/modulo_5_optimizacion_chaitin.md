# Módulo 5 — Optimización, Chaitin y menú interactivo

## 1. Objetivo académico

Este módulo implementa el Hito III, Fase 8 sobre la RI tipada del Módulo 4. La
misma traducción AST → RI alimenta dos caminos físicos:

```text
RI -> backend de referencia -> LCERO_IR 1
RI -> CFG -> vivacidad -> interferencia -> Chaitin -> backend optimizado
```

El backend de referencia se conserva como especificación ejecutable sencilla.
El optimizado reduce ubicaciones de memoria y tráfico cuando la vivacidad lo
permite. Ambos resultados pasan por el mismo `LciValidator` y por la publicación
transaccional. No existe una sustitución silenciosa si el optimizador falla.

## 2. Grafo de flujo de control

Para una RI `I = (i0,...,i(N-1))`, el grafo es `CFG=(B,E)`. Cada bloque básico
es un intervalo máximo `[begin,end)` con una sola entrada y transferencias solo
al final. Son líderes:

1. `i0`;
2. toda instrucción `LABEL`;
3. cada destino de `JMP` o `JZ`;
4. la instrucción posterior a `JMP`, `JZ` o `HALT`, si existe.

El sucesor de un bloque ordinario es el bloque siguiente. `JMP L` tiene solo el
destino de `L`; `JZ t,L` tiene el destino y la continuación; `HALT` no tiene
sucesores. Las listas de predecesores se obtienen invirtiendo esas aristas. La
tabla `instruction_to_block` materializa la correspondencia entre instrucciones
y vértices del CFG.

El constructor incluye bloques no alcanzables para no ocultar RI defectuosa y
valida etiquetas duplicadas o inexistentes antes del análisis.

## 3. Vivacidad

Para cada instrucción se extraen `USE(i)` y `DEF(i)` desde la variante C++, no
desde texto. Las constantes, cargas y operaciones definen su destino; `STORE`,
`PRINT` y `JZ` usan una fuente; las operaciones binarias y comparaciones usan
ambas fuentes.

Para un bloque recorrido en orden:

```text
USE(B) = usos anteriores a una definición local
DEF(B) = temporales definidos en B
```

El análisis hacia atrás resuelve hasta punto fijo:

```text
OUT(B) = unión IN(S), para S en Succ(B)
IN(B)  = USE(B) unión (OUT(B) - DEF(B))
```

La transferencia es monótona sobre el retículo finito `P(Temp)^B`; por ello la
iteración termina. El código recorre los bloques en orden inverso para acelerar,
pero la solución no depende de ese orden. Después reconstruye por instrucción:

```text
OUT(i) = conjunto vivo después de i
IN(i)  = USE(i) unión (OUT(i) - DEF(i))
```

Se comprueba que el `IN` reconstruido al comienzo de cada bloque coincide con
el punto fijo del bloque.

### 3.1 Ejemplo completo con `while`

Para:

```text
int x;
x = 0;
while (x < 5) {
    print(x);
    x = x + 1;
}
```

la RI numerada es:

```text
0  t0=CONST 0       1  STORE x,t0
2  LABEL L0         3  t1=LOAD x
4  t2=CONST 5       5  t3=CMP_LT t1,t2
6  JZ t3,L1         7  t4=LOAD x
8  PRINT t4         9  t5=LOAD x
10 t6=CONST 1       11 t7=ADD t5,t6
12 STORE x,t7       13 JMP L0
14 LABEL L1         15 HALT
```

Bloques y aristas reales:

| Bloque | Intervalo | Sucesores | USE | DEF | IN | OUT |
|---|---:|---|---|---|---|---|
| B0 | `[0,2)` | B1 | `{}` | `{t0}` | `{}` | `{}` |
| B1 | `[2,7)` | B2,B3 | `{}` | `{t1,t2,t3}` | `{}` | `{}` |
| B2 | `[7,14)` | B1 | `{}` | `{t4,t5,t6,t7}` | `{}` | `{}` |
| B3 | `[14,16)` | — | `{}` | `{}` | `{}` | `{}` |

Los conjuntos entre bloques son vacíos porque el generador guarda variables y
vuelve a cargarlas; esto no elimina la vivacidad dentro de cada expresión. En
B1, por ejemplo:

| i | IN(i) | OUT(i) |
|---:|---|---|
| 3 | `{}` | `{t1}` |
| 4 | `{t1}` | `{t1,t2}` |
| 5 | `{t1,t2}` | `{t3}` |
| 6 | `{t3}` | `{}` |

La arista B2 → B1 demuestra que el punto fijo procesa correctamente un salto
hacia atrás aunque en este programa concreto no sobrevivan temporales entre
bloques.

## 4. Grafo de interferencia

El grafo no dirigido `G=(V,G_E)` tiene un vértice por temporal. Se agrega una
arista cuando dos temporales están vivos en el mismo punto, cuando una definición
interfiere con `OUT(i)`, o cuando dos fuentes deben leerse simultáneamente. Se
eliminan autointerferencias y cada inserción se realiza en ambas adyacencias.

No se completa el grafo indiscriminadamente. En el ejemplo anterior:

```text
V = {t0,t1,t2,t3,t4,t5,t6,t7}
G_E = {{t1,t2}, {t5,t6}}
```

`t3` puede reutilizar un registro de `t1` o `t2` porque el contrato garantiza
que las fuentes se leen antes de escribir el destino. La frecuencia de usos se
registra como insumo del costo de spilling.

## 5. Heurística de Chaitin

Se reservan `R0` y `R1` como auxiliares y se colorea con:

```text
K = 2, color 0 -> R2, color 1 -> R3
```

### 5.1 Simplificación

Mientras existe un vértice de grado actual menor que `K`, se selecciona el de
menor identificador, se elimina y se apila. Si ninguno es simplificable, se
marca como candidato potencial de spill el que minimiza:

```text
cost(v) = use_frequency(v) / max(1, degree(v))
```

La comparación usa productos cruzados enteros. Los empates se resuelven por
identificador, por lo que el resultado es determinista.

### 5.2 Reconstrucción

Se desapila en orden inverso y se escoge el menor color ausente en los vecinos
ya coloreados. Si ambos colores están ocupados, el temporal se derrama. Un
candidato potencial puede colorearse finalmente si la reconstrucción lo
permite; la heurística no fuerza spills innecesarios.

En el `while` anterior, el orden de simplificación es
`t0,t1,t2,t3,t4,t5,t6,t7`; al reconstruir, `t1`/`t2` y `t5`/`t6` reciben colores
distintos y no hay spills. Para el triángulo `{t0-t1,t1-t2,t0-t2}` con K=2, la
prueba automatizada asigna dos colores y derrama determinísticamente `t0`, el
de menor frecuencia.

## 6. Spilling y asignación física

Si hay `V` variables y `S` temporales derramados:

```text
address(variable_i) = i
spill_slot_j = V + j
MEM = V + S
```

No se reserva memoria para temporales coloreados. Para cada uso derramado se
carga desde su slot en `R0` o `R1`; un destino derramado se calcula en `R0` y se
guarda. Dos fuentes derramadas usan ambos auxiliares. Si una fuente aparece dos
veces, una única materialización sirve para ambas posiciones.

Ejemplo:

```text
LOAD R0, spill(a)
LOAD R1, spill(b)
ADD R0, R0, R1
STORE spill(d), R0
```

Los valores coloreados nunca ocupan `R0/R1`; por tanto una materialización no
puede destruirlos. Una operación puede escribir sobre el registro de una fuente
muerta porque `LCERO_IR 1` lee fuentes antes de escribir el destino. Esta es la
invariante que conecta interferencia, coloreo y emisión.

| RI | Emisión optimizada |
|---|---|
| `t=CONST n` | `CONST loc(t),n`; `STORE` si hay spill |
| `t=LOAD v` | `LOAD loc(t),address(v)`; `STORE` si hay spill |
| `STORE v,t` | materializar `t`; `STORE address(v),reg` |
| `t=OP a,b` | materializar fuentes; `OP loc(t),ra,rb`; guardar si spill |
| `JZ t,L` | materializar `t`; `JZ reg,L` |
| `PRINT t` | materializar `t`; `PRINT reg` |
| `JMP/LABEL/HALT` | traducción directa |

`OP` comprende cuatro operaciones aritméticas y seis comparaciones. `DIV` no se
anticipa ni se pliega: la VM conserva la división truncada y el error por cero.
Los literales siguen siendo cadenas decimales arbitrariamente grandes.

## 7. Menú e integración

Sin argumentos, `main` muestra un menú antes de compilar:

```text
[1] Compilacion normal (sin optimizacion)
[2] Compilacion optimizada (Chaitin)
[0] Salir
```

Entradas inválidas vuelven a solicitar la opción; `0` y EOF terminan con código
0 sin compilar. `InteractiveMenu` recibe `istream`/`ostream`, lo que permite
probarlo sin intervención humana.

Con argumentos no hay lectura interactiva:

```powershell
LCero.Compiler.exe --backend reference
LCero.Compiler.exe --backend optimized
LCero.Compiler.exe programa.lc -o programa.lci --backend optimized
```

Una ruta sin `--backend` usa `optimized`. La consola cotidiana muestra solo el
modo, etapas `[OK]`, errores y resultado. Las métricas permanecen en
`BackendResult`/`CompilationResult` y en el reporte de pruebas.

## 8. Correctitud

La vivacidad es la menor solución del sistema de ecuaciones porque la iteración
parte de vacío, aplica funciones monótonas y termina en un retículo finito. Si
dos valores deben coexistir, el constructor los conecta; la comprobación final
del coloreo impide que vecinos compartan color.

Por inducción sobre las instrucciones RI:

- una ubicación coloreada contiene el valor de su última definición mientras
  esté vivo, pues toda definición simultáneamente viva tiene otro color;
- una ubicación derramada contiene ese valor en su slot después de cada
  definición;
- materializar un spill solo modifica auxiliares no asignables;
- cada operación lee valores equivalentes a `rho` antes de escribir su destino;
- saltos, memoria de variables y `PRINT` conservan orden y destino.

Así, bajo una RI válida y la semántica del contrato, ambos backends simulan la
misma transición abstracta. La suite lo verifica con un ejecutor acotado para
aritmética, ramas, bucles, anidamiento, salida y división por cero. Esto es
evidencia experimental adicional, no una prueba mecanizada universal.

## 9. Complejidad

Sean `N` instrucciones, `B` bloques, `E` aristas CFG, `T` temporales, `G_E`
aristas de interferencia, `K=2` y `P` pasadas hasta punto fijo.

- CFG: `O(N log N + E)` por mapas/conjuntos ordenados.
- Vivacidad: `O(P(B+E)T)` operaciones de conjunto en notación abstracta; con
  `std::set` aparece además el costo logarítmico de inserción. La altura finita
  acota el crecimiento, pero no se afirma linealidad global.
- Interferencia: peor caso `O(N T^2 log T)` al formar cliques vivos.
- Simplificación/selección: `O(T^2 + G_E log T)` en esta implementación por los
  barridos deterministas y borrados en conjuntos.
- Reconstrucción: `O(T + G_E)` consultas de colores, más costos de conjunto.
- Emisión: `O(N + D)`, con `D` longitud total de literales decimales.

Son costos de compilación; no describen el número de iteraciones de un `while`
en la VM.

## 10. Coloreo y NP-completitud

El problema de decisión de K-coloreo pregunta si existe una asignación de a lo
sumo K colores con extremos de cada arista distintos. Una asignación propuesta
se verifica en tiempo polinómico, por lo que pertenece a NP. Para todo K fijo
mayor o igual a 3, el problema general es NP-completo mediante reducciones
polinómicas conocidas; encontrar un coloreo óptimo hereda esa dificultad.

No se afirma que 2-coloreo sea NP-completo: decidir si un grafo es bipartito y
2-coloreable se resuelve en tiempo polinómico. Aquí K=2, pero un grafo que no es
bipartito exige decidir qué vértices derramar y con qué costo; Chaitin se usa
como marco heurístico determinista para simplificación, selección y
reconstrucción. Tampoco se afirma que los grafos particulares de L-Cero sean una
clase NP-completa ni se inventa una reducción para ellos.

## 11. Limitaciones

No hay coalescing de `MOV`, eliminación de código muerto, ponderación por
profundidad de bucle, recoloreo ni iteración de reescritura del grafo después de
spilling. K=2 prioriza seguridad sobre agresividad. El ejecutor acotado vive
solo en pruebas y usa `int64_t`, por lo que los literales grandes se verifican
estructuralmente. La VM independiente aún no fue integrada.

## AUDITORÍA DEL MÓDULO 5 — INFORMACIÓN PARA REVISIÓN EXTERNA

### A. Arquitectura

Se añadieron `register_allocation.hpp/.cpp`, `optimized_backend.hpp/.cpp` y
`console_menu.hpp/.cpp`; se modificaron opciones, coordinador, `main`, proyectos
Visual Studio, backend de referencia y pruebas. `ControlFlowGraphBuilder`,
`LivenessAnalyzer`, `InterferenceGraphBuilder` y `ChaitinAllocator` son
independientes del emisor. `ReferenceBackend` permanece disponible y consume la
misma `ir::Program`.

### B. CFG y vivacidad

El ejemplo de la sección 3.1 aporta cuatro bloques, cuatro aristas, USE/DEF/IN/
OUT completos y vivacidad por instrucción. El ciclo B2 → B1 fuerza iteración
hasta punto fijo. El resultado registra `fixed_point_iterations` y comprueba la
coincidencia bloque/instrucción.

### C. Interferencia

Para ese programa los ocho vértices y las aristas exactas son
`{t1,t2}` y `{t5,t6}`. Se agregan por coexistencia de fuentes. Cada arista se
inserta simétricamente; las pruebas recorren ambas adyacencias y rechazan lazos.
Una prueba adicional conserva un temporal vivo a través de una arista de
retorno y comprueba que una definición del cuerpo no pueda pisarlo.

### D. Chaitin

K=2; `R2/R3` son asignables y `R0/R1` auxiliares. La simplificación elige el
menor identificador con grado `<2`; si no existe, minimiza frecuencia/grado con
desempate por id. La reconstrucción usa el menor color libre. El ejemplo del
`while` tiene orden `0,1,2,3,4,5,6,7`, colores distintos para los dos pares de
aristas y cero spills. El triángulo de prueba selecciona `t0`, asigna los dos
colores restantes y ubica `t0` en el primer slot de spill. Se verifica que toda
arista con ambos extremos coloreados tenga colores diferentes.

### E. Backend

La tabla de la sección 6 cubre cada operación. Dos spills usan `R0/R1`; un
destino spill usa `R0` después de leer ambas fuentes. Los registros coloreados
no se usan como auxiliares. Para `print(5);` el modo optimizado genera:

```text
LCERO_IR 1
MEM 0
CONST R2, 5
PRINT R2
HALT
```

El de referencia usa un slot, un `STORE` y un `LOAD`; ambos son válidos para el
contrato congelado.

### F. Métricas reales

Datos obtenidos de `LCero.Compiler.Tests.exe --optimizer-report`:

| Programa | RI | Temp | B/E | V/G_E | Spills | Ref MEM/inst/L+S | Opt MEM/inst/L+S |
|---|---:|---:|---:|---:|---:|---:|---:|
| constante | 3 | 1 | 1/0 | 1/0 | 0 | 1/5/2 | 0/3/0 |
| aritmética | 11 | 8 | 1/0 | 8/5 | 1 | 9/27/18 | 2/13/4 |
| condicional | 16 | 8 | 4/4 | 8/2 | 0 | 9/30/18 | 1/14/2 |
| bucle | 16 | 8 | 4/4 | 8/2 | 0 | 9/30/21 | 1/14/5 |
| anidado | 17 | 10 | 5/6 | 10/3 | 0 | 11/35/25 | 1/15/5 |

`L+S` es la suma de `LOAD` y `STORE`. Son mediciones de estos cinco casos, no
una garantía de mejora para todo programa.

### G. Verificación

Se conservaron las 60 pruebas y se añadieron 10 para CFG, vivacidad,
interferencia, Chaitin coloreable/no coloreable, spilling físico, equivalencia,
literales grandes, argumentos, menú simulado y pipeline optimizado. Resultado:
70/70. Debug|x64 compila con 0 errores y 0 advertencias.

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' '.\LCero-Compilador.slnx' /t:Build /m /p:Configuration=Debug /p:Platform=x64
.\bin\x64\Debug\LCero.Compiler.Tests.exe
.\bin\x64\Debug\LCero.Compiler.Tests.exe --optimizer-report
.\bin\x64\Debug\LCero.Compiler.exe --backend optimized
```

Las opciones 0, 1, 2, inválida y EOF se prueban con streams simulados. La VM
externa no está integrada y no se afirma lo contrario.

### H. Matemática

Las secciones 2–6 presentan CFG, ecuaciones, punto fijo, interferencia, Chaitin
y el invariante de registros. Las secciones 8–10 distinguen argumentos de
corrección, comprobaciones experimentales, complejidad y la relación precisa
con K-coloreo. No hay prueba mecanizada ni prueba de optimalidad de la
heurística.

### I. Próximos pasos

El Módulo 6 debe integrar la VM independiente y realizar profiling empírico.
Podrá explorar costos ponderados por bucles, coalescing y recoloreo sin cambiar
la RI ni `LCERO_IR 1`. Los riesgos son diferencias de interpretación de enteros
grandes, errores dinámicos o conteo de pasos; los vectores contractuales y ambos
backends sirven como oráculos cruzados. El contrato de la VM no fue modificado.
