# Módulo 1 — Análisis léxico y AFD mínimo de L-Cero

## 1. Propósito y alcance

Este módulo implementa el Hito I, Fase 1. Parte de la especificación regular
de los lexemas, construye manualmente un AFN combinado, aplica construcción por
subconjuntos, completa el AFD con un sumidero y ejecuta refinamiento de
particiones preservando la **clase de token**. El lexer recorre el AFD mínimo
resultante; `std::regex` no participa del reconocimiento.

El resultado comprobado es:

| Construcción | Estados | Observación |
|---|---:|---|
| AFN combinado | 67 | `N0` a `N66`, con transiciones ε |
| AFD determinado y completo | 40 | `D0` a `D39`; `D39` es el sumidero |
| AFD mínimo observable | 40 | `M0` a `M39`; `M21` es el sumidero |

Que ambos AFD tengan 40 estados no significa que se omitió la minimización: la
partición inicial de 25 bloques se refina hasta 40 singletons. Por lo tanto no
existían dos estados alcanzables equivalentes bajo la salida observable del
lexer.

No se implementan parser, AST, semántica, RI, optimización ni emisión `.lci`.

## 2. Alfabeto formal

Sea

\[
\Sigma=L\cup D\cup O\cup S\cup B,
\]

donde:

\[
\begin{aligned}
L &= \{a,\ldots,z,A,\ldots,Z\},\\
D &= \{0,\ldots,9\},\\
O &= \{+, -, *, /, =, !, <, >\},\\
S &= \{(,),\{,\},;,\_\},\\
B &= \{\text{SPACE},\text{TAB},\text{CR},\text{LF}\}.
\end{aligned}
\]

Los conjuntos son disjuntos y
`|Σ| = 52 + 10 + 8 + 6 + 4 = 80`. `EOF` es un marcador interno y no
pertenece a `Σ`.

`Σ` es un alfabeto de **caracteres**. El alfabeto de tokens que consumirá el
parser es otro conjunto: `KW_INT`, `ID`, `NUMBER`, `PLUS`, etc. Por ejemplo,
`int` contiene tres símbolos de `Σ` pero el lexer emite un solo `KW_INT`; `==`
contiene dos símbolos pero produce un solo `EQ`.

El archivo se lee como bytes. Solo los bytes ASCII de `Σ` son admitidos; el
primer byte fuera de ese conjunto genera error léxico. Esto implementa la
política UTF-8 con contenido léxico ASCII del documento maestro: una letra
acentuada codificada en UTF-8 es rechazada, no reinterpretada como letra.

## 3. Tokens y lenguajes regulares

Las definiciones regulares son:

```text
ID         = [A-Za-z_][A-Za-z0-9_]*
NUMBER     = [0-9]+
WHITESPACE = [ \t\r\n]+
```

Las palabras reservadas son lenguajes singleton: `{int}`, `{if}`, `{else}`,
`{while}` y `{print}`. Los operadores y delimitadores también son lenguajes
finitos singleton. La unión finita de lenguajes regulares es regular, por lo
que el lenguaje de lexemas reconocibles es regular.

| Familia | Tipos observables |
|---|---|
| Reservadas | `KW_INT`, `KW_IF`, `KW_ELSE`, `KW_WHILE`, `KW_PRINT` |
| Generales | `ID`, `NUMBER` |
| Aritméticos | `PLUS`, `MINUS`, `STAR`, `SLASH` |
| Comparadores | `EQ`, `NE`, `LT`, `GT`, `LE`, `GE` |
| Asignación | `ASSIGN` |
| Delimitadores | `LPAREN`, `RPAREN`, `LBRACE`, `RBRACE`, `SEMICOLON` |
| Internos | `WHITESPACE` descartado y `EOF` agregado fuera del AFD |

El signo no forma parte de `NUMBER`: `-25` produce `MINUS`, `NUMBER("25")`.
El lexema numérico permanece como texto y no se limita a un tipo entero nativo.

No existen reglas para comentarios, punto decimal, cadenas, `&&`, `||` o `!`
aislado. `//` son dos tokens `SLASH`; si la gramática no los admite, será un
error sintáctico posterior.

## 4. Construcción manual del AFN

### 4.1 Procedimiento

Para cada literal `a₁…aₖ` se crea una rama lineal con `k+1` estados:

\[
r_0\xrightarrow{a_1}r_1\xrightarrow{a_2}\cdots
\xrightarrow{a_k}r_k,
\]

y `rₖ` recibe la clase de aceptación correspondiente. Para `ID`, `NUMBER` y
`WHITESPACE` se construyen manualmente los estados inicial/final y sus bucles de
Kleene positivo. Un nuevo estado `N0` se une por ε al inicio de cada rama. Esta
es la construcción estándar de unión por ε; es equivalente a la parte de unión
de Thompson, pero aquí los tres patrones con clases de caracteres se expanden
directamente sobre el alfabeto finito.

La numeración exacta de las 24 ramas está en
[`automatas/afn_ramas.csv`](automatas/afn_ramas.csv). Las transiciones ε son:

```text
Δ(N0, ε) = {N1,N5,N8,N13,N19,N25,N27,N29,N31,N33,N35,N37,
             N39,N42,N45,N47,N49,N52,N55,N57,N59,N61,N63,N65}
```

Las transiciones de `ID`, `NUMBER` y blancos son:

```text
Δ(N25, [A-Za-z_])    = {N26}
Δ(N26, [A-Za-z0-9_]) = {N26}
Δ(N27, [0-9])        = {N28}
Δ(N28, [0-9])        = {N28}
Δ(N29, B)            = {N30}
Δ(N30, B)            = {N30}
```

Las demás transiciones son las cadenas literales del CSV; toda transición no
enumerada devuelve el conjunto vacío.

### 4.2 Quíntupla del AFN

\[
N=(Q_N,\Sigma,\Delta,N0,F_N),
\]

con:

```text
Q_N = {N0, N1, ..., N66}
F_N = {N4,N7,N12,N18,N24,N26,N28,N30,N32,N34,N36,N38,
       N41,N44,N46,N48,N51,N54,N56,N58,N60,N62,N64,N66}
```

`F_N` no es solamente booleano: cada estado lleva `(tipo, prioridad)`. Las
reservadas tienen prioridad 0, los símbolos fijos 10, los patrones generales
`ID`/`NUMBER` 100 y blancos 200. Solo hay una superposición relevante:
`int`, `if`, `else`, `while` y `print` también pertenecen al lenguaje de `ID`.
La menor prioridad numérica selecciona la palabra reservada. Si aparece un
carácter adicional (`while2`), la rama literal muere pero la rama `ID` sigue.

La implementación de `epsilon_closure` recorre por pila todas las aristas ε; la
de `move(S,a)` une los destinos etiquetados `a` de los estados de `S`.

## 5. Determinización por subconjuntos

Para un conjunto `T ⊆ Q_N` se definen:

\[
E(T)=\text{cerradura-}\varepsilon(T),\qquad
move(T,a)=\bigcup_{q\in T}\Delta(q,a).
\]

El estado inicial determinista es:

\[
D0=E(\{N0\}).
\]

Cada transición se calcula con:

\[
\delta_D(T,a)=E(move(T,a)).
\]

Los subconjuntos nuevos se insertan en una cola y se procesan hasta no descubrir
ninguno nuevo. Los 39 subconjuntos no vacíos alcanzables y el conjunto vacío
sumidero se encuentran en
[`automatas/afd_subconjuntos.csv`](automatas/afd_subconjuntos.csv).

Ejemplo para la superposición `if`/`ID`:

```text
δ(D0, i) = {N2,N6,N26} = D16          aceptación ID
δ(D16,f) = {N7,N26}    = D26          aceptación KW_IF por prioridad
δ(D26,0) = {N26}       = D14          aceptación ID
```

Por ello `if` es `KW_IF`, mientras `if0` es un único `ID`. Para `!=`:

```text
δ(D0, !)  = {N43} = D2                no final
δ(D2, =)  = {N44} = D21               aceptación NE
δ(D2, a)  = ∅     = D39               sumidero
```

### 5.1 Quíntupla del AFD completo

\[
D=(Q_D,\Sigma,\delta_D,D0,F_D,\lambda_D),
\]

donde `Q_D={D0,…,D39}`, `D39` representa `∅`, `δ_D` es total sobre `Σ`,
`F_D=Q_D\{D0,D2,D39}` y `λ_D` asigna a cada final el tipo consignado en el
CSV. Se escribe `λ_D` de manera explícita porque dos estados finales solo son
equivalentes para el lexer si producen el mismo tipo.

## 6. Minimización con salida observable

### 6.1 Equivalencia usada

Para estados `p,q` del AFD completo:

\[
p\equiv q \iff
\forall w\in\Sigma^*,\;
\lambda^*(p,w)=\lambda^*(q,w),
\]

donde `λ*` devuelve el tipo aceptado después de consumir `w`, o `⊥` si no hay
aceptación. Esta relación es más estricta que preservar solo la unión de
lexemas: impide fusionar, por ejemplo, un estado `PLUS` con uno `MINUS`.

### 6.2 Refinamiento

`P0` agrupa estados con igual salida inmediata. Contiene un bloque no final,
un bloque con los 14 estados `ID` y un singleton por cada una de las otras 23
clases finales: 25 bloques en total.

En cada ronda se usa la firma:

\[
sig_i(q)=([q]_{P_i},[\delta(q,a_1)]_{P_i},\ldots,
[\delta(q,a_{80})]_{P_i}).
\]

Dos estados permanecen juntos si y solo si sus firmas coinciden. El proceso
termina cuando `Pᵢ₊₁=Pᵢ`. La traza completa está en
[`automatas/minimizacion_particiones.txt`](automatas/minimizacion_particiones.txt):

| Partición | Bloques |
|---|---:|
| `P0` | 25 |
| `P1` | 32 |
| `P2` | 35 |
| `P3` | 38 |
| `P4` | 40 singletons |

`P4` es estable. Como el AFD era completo y todos sus estados eran alcanzables,
el teorema de refinamiento de particiones establece que esos bloques son las
clases de equivalencia observacional. Al ser todos singletons, no existe un par
fusionable y el AFD de 40 estados es mínimo bajo la clasificación del lexer.
Una segunda prueba automática aplica el algoritmo de tabla de pares
distinguibles y confirma que no queda ningún par equivalente.

### 6.3 Quíntupla del AFD mínimo

\[
M=(Q_M,\Sigma,\delta_M,M0,F_M,\lambda_M),
\]

con `Q_M={M0,…,M39}`, inicio `M0`, sumidero `M21`,
`F_M=Q_M\{M0,M2,M21}` y etiquetas `λ_M` en la columna `aceptacion` de la
matriz. La columna `origen_afd` da la correspondencia exacta entre `Mᵢ` y el
singleton de `P4` que representa.

## 7. Matriz de transición definitiva

La matriz ejecutada está en
[`automatas/afd_minimo_transiciones.csv`](automatas/afd_minimo_transiciones.csv).
Tiene 40 filas y 27 columnas de símbolos. Las 80 columnas originales se
agruparon por equivalencia global:

\[
a\sim_\Sigma b \iff \forall q\in Q_M,\;\delta_M(q,a)=\delta_M(q,b).
\]

Las clases exactas están en
[`automatas/clases_simbolos.csv`](automatas/clases_simbolos.csv). No se agrupan
letras relevantes para prefijos de palabras reservadas: `e`, `f`, `h`, `i`,
`l`, `n`, `p`, `r`, `s`, `t` y `w` tienen columnas propias. `C13` contiene las
letras que sí poseen la misma transición desde todos los estados, junto con
`_`. `C8` contiene los diez dígitos y `C0` los cuatro blancos.

`M21` tiene bucles hacia sí mismo para toda `Σ`. Durante reconocimiento de un
lexema completo esta es una matriz total. Durante tokenización, maximal munch
se detiene **antes** de consumir la transición al sumidero para que el carácter
pueda iniciar el token siguiente.

## 8. Correspondencia con el código C++

- `Nfa::build_lexer_nfa` construye las 24 ramas y sus prioridades.
- `Nfa::epsilon_closure` y `Nfa::move` implementan las operaciones formales.
- `Dfa::determinize` enumera los subconjuntos alcanzables y agrega el sumidero.
- `Dfa::minimize` realiza el refinamiento y renumera en BFS desde el inicio.
- `Dfa::minimization_partition_trace` expone la evidencia `P0…P4`.
- `Dfa::equivalent_to` recorre el producto de dos autómatas y compara salidas.
- `Dfa::has_equivalent_states` usa la tabla de pares distinguibles.
- `Lexer::tokenize` recorre exclusivamente `minimized_dfa()`.

Las expresiones regulares son la especificación matemática. El reconocedor no
las evalúa: consulta la tabla `transitions[estado][byte]` construida por los
algoritmos anteriores. Los anexos se extrajeron del mismo objeto de autómata que
usan las pruebas y el lexer.

## 9. Máxima coincidencia y prioridad

Desde cada posición, el lexer comienza en `M0`, avanza mientras no deba entrar
al sumidero y recuerda `(posición,tipo)` de la última aceptación. Si la
extensión falla, emite ese prefijo máximo y continúa desde su fin. Si nunca hubo
aceptación, informa el primer carácter de la secuencia y se detiene.

Ejemplos:

```text
==       -> EQ
=        -> ASSIGN
<=       -> LE
<        -> LT
while    -> KW_WHILE
while2   -> ID
While    -> ID
-25      -> MINUS NUMBER("25")
```

La prioridad solo decide cuando un mismo subconjunto contiene varios finales.
Así, `while` elige `KW_WHILE` sobre `ID`; no altera maximal munch.

## 10. Posiciones y errores

Cada token conserva tipo, lexema original, línea, columna y desplazamiento en
bytes. Línea y columna comienzan en 1. SPACE, TAB y caracteres ordinarios
incrementan la columna en una unidad. `LF` incrementa la línea y restablece la
columna a 1. `CRLF` se consume como un solo salto; un `CR` aislado también es un
salto.

La recuperación es deliberadamente conservadora: se detiene en el primer error
léxico. El diagnóstico se registra en `DiagnosticBag` con categoría `Lexical`,
archivo, línea y columna. Casos representativos:

```text
x = 2 @ 3;   -> error en @
x ! 5;       -> error: ! aislado; solo != es válido
x && y;      -> error en el primer &
12.3         -> NUMBER("12"), luego error en .
á            -> error en el primer byte no ASCII
```

Una secuencia formada por tokens válidos pero incompatible con la gramática no
es error léxico y será responsabilidad del Módulo 2.

## 11. Ejemplos de tokenización

Entrada:

```text
int edad;
edad = -25;
print(edad);
```

Salida:

```text
KW_INT("int") ID("edad") SEMICOLON(";")
ID("edad") ASSIGN("=") MINUS("-") NUMBER("25") SEMICOLON(";")
KW_PRINT("print") LPAREN("(") ID("edad") RPAREN(")")
SEMICOLON(";") EOF("")
```

La entrada predeterminada actual `print(5);` produce seis tokens incluyendo
`EOF` y el ejecutable informa `67/40/40` estados. Su estado general sigue siendo
incompleto porque no existe parser; el código de salida es 2 y no se publica
ningún `.lci`.

## 12. Pruebas y evidencia

La suite ejecutada contiene 30 pruebas: las 11 del Módulo 0 y 19 del lexer y
los autómatas. Cubre reservadas, identificadores, números arbitrariamente
largos, todos los operadores y delimitadores, LF/CRLF, bloques anidados, fuente
vacío, distinción reservada/ID, maximal munch y los errores requeridos.

Las pruebas formales verifican:

1. 67 estados en el AFN combinado.
2. Determinismo y totalidad de ambos AFD.
3. Alcanzabilidad de todos los estados conservados.
4. Clasificación de estados finales y matriz dentro de rango.
5. Equivalencia observable del AFD de subconjuntos y el mínimo mediante el
   producto de estados.
6. Ausencia de pares fusionables mediante tabla de distinguibilidad.
7. Igualdad de clasificación AFN/AFD/AFD mínimo para todas las cadenas de
   longitud 0 a 4 sobre un conjunto representativo de 11 símbolos
   (`11⁰+…+11⁴ = 16105` cadenas).

Resultado verificado en `Debug|x64`: **30/30**, compilación con **0 errores y
0 advertencias**.

Las pruebas exhaustivas finitas complementan, pero no sustituyen, el argumento
formal de determinización y minimización presentado arriba.

## 13. Complejidad preliminar

Sea `m=|Q_N|`, `d=|Q_D|`, `s=|Σ|` y `n` la cantidad de bytes del fuente.

- Construcción del AFN: proporcional al número de estados y transiciones de la
  especificación; para L-Cero es una constante realizada una vez por `Lexer`.
- Determinización: en el peor caso puede producir `2^m` subconjuntos. La
  implementación explora solo los alcanzables y evalúa `s` símbolos por estado.
- Refinamiento de Moore: a lo sumo `d-1` separaciones; con firmas completas, la
  cota directa es `O(d²s)` sin afirmar que sea Hopcroft.
- Reconocimiento: cada byte causa una consulta de tabla de costo constante y,
  como máximo, un carácter de lookahead se reconsidera al iniciar el token
  siguiente. Por ello `T(n)=O(n)`. Todo archivo debe inspeccionarse para emitir
  todos sus tokens, así que `T(n)=Ω(n)`. Bajo el modelo RAM y autómata fijo,
  `T(n)=Θ(n)`.
- El autómata usa espacio constante respecto de `n`. Los tokens y sus lexemas
  copiados ocupan `O(n)`; el contenido fuente leído también ocupa `O(n)`. La
  construcción temporal de autómatas depende de `m,d,s`, no del archivo.

El costo de construcción se mantiene separado del recorrido. El profiling real
queda reservado al Hito III.

## 14. Limitaciones y siguiente fase

- El lexer no decide si la secuencia de tokens forma un programa.
- `EOF` se agrega después del recorrido y no es símbolo del autómata.
- Los blancos se reconocen formalmente, pero no se emiten al parser.
- Se informa solo el primer error léxico.
- No se convierten números a `cpp_int` todavía.
- No existe recuperación sintáctica ni AST.

El Módulo 2 deberá fijar la BNF, calcular FIRST/FOLLOW, demostrar la condición
LL(1), construir la tabla predictiva e implementar parser y AST sin modificar
el contrato léxico aquí documentado.
