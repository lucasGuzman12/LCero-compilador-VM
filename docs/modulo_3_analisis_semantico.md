# L-Cero — Módulo 3: análisis semántico y tabla de símbolos

## 1. Objetivo y alcance

Este módulo completa el Hito I, Fase 3. Recibe el AST válido producido por el
parser y comprueba propiedades que relacionan partes distantes del programa:
unicidad de declaraciones, existencia de variables y asignación definitiva
antes de cada lectura.

La sintaxis responde si una secuencia pertenece al lenguaje generado por la BNF.
La semántica estática responde si los nombres y sus estados respetan el contrato
de L-Cero. Por ejemplo, `int x; print(x);` es sintácticamente correcto, pero se
rechaza semánticamente porque la lectura de `x` no está precedida por una
inicialización garantizada.

No se implementan RI, optimización, asignación de registros, emisión `.lci` ni
ejecución. El contrato `LCERO_IR 1` permanece intacto.

## 2. Modelo matemático

Sea `Var` el universo de nombres de variables representables por tokens `ID`.
Para un programa dado se mantienen:

\[
D \subseteq Var
\]

donde `D` es el conjunto de variables declaradas una única vez, y

\[
I \subseteq D
\]

donde `I` es el conjunto de variables definitivamente inicializadas en un punto
del programa. “Definitivamente” significa que toda ejecución representada por el
análisis que llega a ese punto ha asignado la variable. Después de procesar las
declaraciones:

\[
I_0=\varnothing
\]

El juicio principal es:

\[
D;I\vdash S\Rightarrow I'
\]

Se lee: con declaraciones `D` y estado de entrada `I`, la instrucción `S` es
válida y produce el estado de salida `I'`. Para expresiones se usa:

\[
D;I\vdash e\;ok
\]

El análisis no evalúa condiciones constantes ni valores aritméticos. En
particular, `print(10 / 0);` es semánticamente válido; la división por cero es un
error de ejecución reservado para la VM.

## 3. Tabla de símbolos

La gramática sólo admite declaraciones al inicio, por lo que existe un único
ámbito global. `SymbolTable` usa `std::map<std::string, Symbol>`. Cada símbolo
contiene nombre, tipo único `Integer` y línea, columna y desplazamiento de la
declaración. Las claves del mapa representan concretamente `D`.

Operaciones:

- `insert`: inserta un símbolo nuevo o devuelve el símbolo previo sin reemplazarlo;
- `find` y `contains`: consultan una declaración;
- `declared_names`: materializa `D` para auditoría e invariantes;
- `symbols` y `size`: permiten inspección de sólo lectura.

Para dos declaraciones de `x`, la segunda produce un diagnóstico en su propia
ubicación que también informa línea y columna de la primera. No existen ámbitos
locales, ocultamiento ni creación implícita por asignación.

## 4. Reglas formales

### 4.1 Declaraciones y expresiones

Una declaración nueva extiende `D`; una repetida es error:

\[
\frac{x\notin D}{D\vdash int\;x;\Rightarrow D\cup\{x\}}
\qquad
\frac{x\in D}{D\vdash int\;x;\Rightarrow error_{duplicada}}
\]

Los literales son siempre válidos, con independencia de su magnitud:

\[
\frac{}{D;I\vdash NUMBER\;ok}
\]

Una referencia exige declaración e inicialización:

\[
\frac{x\in D\quad x\in I}{D;I\vdash x\;ok}
\]

Si `x` no pertenece a `D` se informa “variable no declarada”; si pertenece a
`D-I`, “variable no inicializada”. Operaciones binarias y menos unario son
válidos cuando lo son sus operandos:

\[
\frac{D;I\vdash e_1\;ok\quad D;I\vdash e_2\;ok}
     {D;I\vdash e_1\;op\;e_2\;ok}
\qquad
\frac{D;I\vdash e\;ok}{D;I\vdash -e\;ok}
\]

Una condición valida ambas expresiones con el mismo estado de entrada; el
comparador no modifica `I`.

### 4.2 Asignación y `print`

El lado derecho se valida antes de agregar el destino:

\[
\frac{x\in D\quad D;I\vdash e\;ok}
     {D;I\vdash x=e;\Rightarrow I\cup\{x\}}
\]

Por eso `int x; x = x + 1;` se rechaza. Para impresión:

\[
\frac{D;I\vdash e\;ok}
     {D;I\vdash print(e);\Rightarrow I}
\]

### 4.3 Secuencias

Las instrucciones se recorren en el orden del AST:

\[
\frac{D;I_0\vdash S_1\Rightarrow I_1\quad
      D;I_1\vdash S_2\Rightarrow I_2\quad\cdots\quad
      D;I_{n-1}\vdash S_n\Rightarrow I_n}
     {D;I_0\vdash S_1;\ldots;S_n\Rightarrow I_n}
\]

### 4.4 `if` con `else`

La condición se valida con `I`. Las ramas parten de copias independientes del
mismo estado:

\[
\frac{D;I\vdash c\;ok\quad
      D;I\vdash A\Rightarrow I_A\quad
      D;I\vdash B\Rightarrow I_B}
     {D;I\vdash if(c)\{A\}\;else\;\{B\}\Rightarrow I_A\cap I_B}
\]

La intersección conserva únicamente las inicializaciones garantizadas por ambos
caminos.

### 4.5 `if` sin `else`

La alternativa implícita es no ejecutar el bloque:

\[
\frac{D;I\vdash c\;ok\quad D;I\vdash A\Rightarrow I_A}
     {D;I\vdash if(c)\{A\}\Rightarrow I_A\cap I}
\]

Como ninguna regla desinicializa variables, `I` está incluido en `I_A` y la
salida equivale a `I`. Esto se mantiene incluso para condiciones constantes
verdaderas: no existe propagación de constantes en este módulo.

### 4.6 `while`

El cuerpo puede ejecutarse cero veces. Se analiza para encontrar errores
internos, pero sus nuevas inicializaciones no salen del bucle:

\[
\frac{D;I\vdash c\;ok\quad D;I\vdash B\Rightarrow I_B}
     {D;I\vdash while(c)\{B\}\Rightarrow I}
\]

La misma regla se aplica recursivamente a bucles anidados. No se intenta probar
terminación ni calcular un número de iteraciones.

## 5. Propiedades fundamentales

### 5.1 Preservación de `I ⊆ D`

Se demuestra por inducción estructural sobre instrucciones, suponiendo
`I ⊆ D` al entrar:

- `print` y `while` devuelven `I`, luego preservan el invariante.
- Una asignación sólo agrega `x` después de comprobar `x ∈ D`; por tanto
  `I ∪ {x} ⊆ D`.
- Una secuencia preserva el invariante por hipótesis inductiva aplicada en orden.
- Cada rama de un `if` produce subconjuntos de `D` por hipótesis inductiva; su
  intersección también es subconjunto de `D`.
- Un `if` sin `else` devuelve una intersección con `I`, nuevamente incluida en `D`.

La implementación comprueba además el invariante antes y después de cada
transferencia exitosa. Una violación se clasificaría como error interno.

### 5.2 Monotonía

Para `D` fijo, considere estados `I ⊆ J ⊆ D`. Si una expresión es válida bajo
`I`, también lo es bajo `J`, porque agregar variables inicializadas no invalida
referencias. Sobre el dominio donde una transferencia está definida:

\[
F_S(I)\subseteq F_S(J)
\]

La propiedad se prueba estructuralmente: unión con un singleton, identidad,
composición e intersección son monótonas. Esto no significa que todo programa
aceptado con `J` lo sea con `I`; una lectura puede requerir precisamente un
elemento de `J-I`.

### 5.3 Corrección conservadora

Hipótesis: las instrucciones no desinicializan variables y las únicas ramas de
control son las representadas por `if` y `while`. Por inducción estructural, si
`x ∈ I_salida`, entonces `x` fue inicializada en todo camino modelado que llega a
la salida:

- la asignación establece `x` en su único camino normal;
- la secuencia compone garantías;
- la intersección de `if/else` exige la garantía en ambas ramas;
- `if` sin `else` considera el camino que omite el bloque;
- `while` considera siempre la ejecución de cero iteraciones.

El análisis puede rechazar programas seguros cuya seguridad dependa del valor de
una condición, pero no acepta una lectura basándose sólo en que una rama o bucle
parezca ejecutarse. No demuestra terminación, ausencia de división por cero ni
propiedades dinámicas generales.

## 6. Correspondencia con C++

| Regla | Implementación |
|---|---|
| Construcción de `D` y duplicados | `SymbolTable::insert`, `SemanticAnalyzer::declare_all` |
| Referencias, unarios y binarios | `validate_expression` |
| Condiciones | `validate_condition` |
| Asignación y `print` | ramas respectivas de `analyze_statement` |
| Secuencia y bloques | `analyze_statements`, `analyze_block` |
| `if/else` | copias de entrada y `std::set_intersection` |
| `if` sin `else` | análisis del bloque y restauración de entrada |
| `while` | análisis de cuerpo sobre copia y restauración de entrada |
| `I ⊆ D` | `validate_invariant` |
| Primer error | retornos booleanos y `error_reported_` |

`InitializationSet` es `std::set<std::string>`. Cada `TransferRecord` conserva
clase de instrucción, posición, entrada, salida y estados de ramas. Estas trazas
permiten contrastar las transferencias matemáticas sin alterar el AST. El
analizador recibe `const ast::Program&`; no copia ni toma propiedad del árbol.

## 7. Ejemplos con evolución de estados

### Ejemplo 1: secuencia válida

```text
int x;
int y;
x = 5;
y = x + 1;
print(y);
```

\[
D=\{x,y\},\quad
I_0=\varnothing\rightarrow I_1=\{x\}
\rightarrow I_2=\{x,y\}\rightarrow I_3=\{x,y\}
\]

### Ejemplo 2: ramas válidas

```text
int x;
if (5 > 3) { x = 10; } else { x = 20; }
print(x);
```

Ambas ramas parten de `I0 = ∅`. Producen `IA = {x}` e `IB = {x}`; por tanto
`IA ∩ IB = {x}` y la impresión es válida.

### Ejemplo 3: `if` sin garantía

```text
int x;
if (1 == 1) { x = 10; }
print(x);
```

El bloque produce `{x}`, pero la rama implícita produce `∅`:

\[
I_{salida}=\{x\}\cap\varnothing=\varnothing
\]

La lectura final falla aunque la condición sea matemáticamente verdadera.

### Ejemplo 4: inicialización sólo en `while`

```text
int x;
while (1 == 0) { x = 5; }
print(x);
```

El cuerpo se valida y produciría `{x}`, pero la salida del bucle es su entrada
`∅`; la impresión falla. El análisis contempla la ejecución de cero iteraciones.

### Ejemplo 5: error de orden

```text
int x;
print(x);
x = 1;
```

La primera instrucción se analiza con `I0 = ∅` y falla. La asignación posterior
no puede justificar retroactivamente la lectura.

## 8. Diagnósticos e integración

Se emite exactamente el primer error semántico, con categoría `Semantic`, ruta,
línea y columna. Clases diferenciadas:

```text
Declaracion duplicada de 'x'; la declaracion anterior esta en linea 1, columna 5.
Variable no declarada: 'y'.
Variable no inicializada: 'x'.
```

El pipeline es lectura → lexer → parser/AST → semántica. Una etapa fallida evita
las posteriores. Tras éxito semántico se informa cantidad de símbolos y
variables definitivamente inicializadas al final, pero se devuelve
`NotImplemented` porque aún no existe RI. La salida previa se invalida al inicio
y nunca se publica un `.lci`.

## 9. Complejidad preliminar

Sea `n` la cantidad de nodos del AST, `v` la cantidad de variables y `L` la suma
de longitudes de nombres procesados. Con `std::map` y `std::set`, inserción y
consulta cuestan `O(log v)` comparaciones, además del costo de comparar cadenas.
Construir la tabla cuesta `O(v log v + L)` y recorrer expresiones e instrucciones
sin combinaciones cuesta `O(n log v + L)`.

Cada estructura de control copia hasta dos conjuntos y, para `if/else`, los
intersecta: `O(v)` por estructura. Por ello una cota explícita es

\[
O(n\log v+n v+L),
\]

que en el peor caso `v=O(n)` resulta `O(n²)`. No se afirma `Θ(n)` ignorando las
copias. La tabla y el estado corriente ocupan `O(v)`, la recursión puede mantener
`O(hv)` datos temporales para profundidad `h`, y las trazas de auditoría guardan
estados por transferencia, con cota `O(nv)`. Un futuro módulo puede representar
variables por índices y usar bitsets, o desactivar trazas fuera de pruebas, sin
cambiar las reglas matemáticas.

## 10. Jerarquía de Chomsky y LBA

El lexer reconoce lenguajes regulares (tipo 3) mediante autómatas finitos. El
parser reconoce la estructura libre de contexto (tipo 2) mediante una gramática
LL(1). Esa estructura por sí sola no relaciona una referencia con una
declaración arbitrariamente distante ni recuerda si todos los caminos previos
inicializaron el mismo nombre.

Con un alfabeto y codificación finitos, las restricciones de igualdad de nombres
y asignación definitiva pueden formularse como condiciones sensibles al
contexto. Un autómata linealmente acotado dispone de memoria proporcional a la
entrada y puede, en principio, marcar o volver a recorrer nombres codificados
para decidir varias de estas relaciones. Esto explica la conexión académica con
lenguajes tipo 1: se necesita memoria dependiente de la entrada, no sólo la pila
sintáctica usada para derivar la BNF.

Sin embargo, una tabla de símbolos no es literalmente un LBA y no todo análisis
semántico requiere exactamente ese modelo. Esta implementación es un algoritmo
sobre AST con mapas y conjuntos, y sus trazas/copias pueden usar espacio
cuadrático; no se presenta como simulador de LBA. Para afirmar formalmente que un
lenguaje semánticamente válido completo es tipo 1 deben fijarse la codificación,
la cota espacial y todas las propiedades incluidas. La relación aquí establecida
es de capacidad y dependencia contextual, no una identidad entre mecanismos.

## 11. Pruebas

Se conservan las 42 pruebas de infraestructura, lexer, parser, AST y gramática.
Se agregan 11 pruebas que cubren:

- inserción, consulta, tipo y ubicación original en la tabla;
- programas válidos sin variables, secuenciales y anidados;
- las tres clases de diagnóstico semántico;
- autolectura en primera asignación;
- condición y aritmética con variables sin inicializar;
- intersección de ramas, `if` sin `else` y `while` conservadores;
- visibilidad global, orden de instrucciones y errores anidados;
- literal arbitrariamente grande y aceptación de `10 / 0`;
- trazas exactas de ramas y preservación automática de `I ⊆ D`;
- integración, detención del pipeline y ausencia de `.lci`.

Resultado verificado: **53/53 pruebas aprobadas**.

## 12. Limitaciones y siguientes fases

No hay ámbitos locales, tipos adicionales, constantes, funciones ni
inicialización automática porque el lenguaje no los define. No se advierten
variables sin uso. No hay evaluación constante, detección estática de división
por cero ni prueba de terminación. El primer error detiene el análisis.

El Módulo 4 podrá consumir simultáneamente el AST validado y `SymbolTable` para
construir `LCERO_IR 1`. La vida del AST no se transfiere al analizador, de modo
que permanece disponible durante el pipeline.

## AUDITORÍA DEL MÓDULO 3 — INFORMACIÓN PARA REVISIÓN EXTERNA

### A. Modelo semántico

- `D`: claves de `SymbolTable`, variables declaradas globalmente una vez.
- `I`: `std::set<std::string>`, variables definitivamente inicializadas.
- Estado inicial: `I0 = ∅` después de todas las declaraciones.
- Invariante: `I ⊆ D`, comprobado en cada frontera de transferencia exitosa.
- Asignación: valida RHS y destino; salida `I ∪ {x}`.
- `print`: valida expresión; salida `I`.
- Secuencia: composición en orden.
- `if/else`: ramas desde la misma entrada; salida por intersección.
- `if` sin `else`: salida igual a entrada.
- `while`: valida condición y cuerpo; salida igual a entrada.
- No existe inicialización implícita ni razonamiento por condiciones constantes.

### B. Tabla de símbolos

`include/lcero/semantic/symbol_table.hpp` y
`src/semantic/symbol_table.cpp` implementan un mapa ordenado nombre→símbolo. El
símbolo almacena nombre, tipo entero y posición. `insert` distingue inserción de
duplicación y devuelve el registro previo; `find`, `contains`, `size`,
`declared_names` y `symbols` permiten consulta. El alcance es único y global.

Ejemplo: al procesar `int x; int x; print(5);`, la primera inserción crea `x` y
la segunda falla señalando su ubicación y la de la primera.

### C. Correspondencia matemática–implementación

`semantic_analyzer.cpp` implementa las reglas. `declare_all` construye `D`;
`validate_expression` aplica los juicios de expresiones; `analyze_statements`
compone; `analyze_statement` implementa cada transferencia. En `if/else` crea
dos copias de entrada y usa `std::set_intersection`; sin `else` restaura la
entrada. En `while` analiza una copia del cuerpo y restaura la entrada. Toda
recursión anidada reutiliza las mismas reglas.

Los errores son deterministas y detienen el recorrido inmediatamente. Los
literales permanecen textuales y una división sólo valida operandos. Las trazas
`TransferRecord` hacen observables entrada, salida y estados de rama.

### D. Ejemplos verificables

Los cinco programas completos de la sección 7 registran respectivamente:

1. `∅ → {x} → {x,y} → {x,y}`: válido.
2. Ramas `{x}` y `{x}`; intersección `{x}`: válido.
3. Ramas `{x}` y `∅`; intersección `∅`; lectura posterior: inválida.
4. Cuerpo de `while` `{x}`, salida `∅`; lectura posterior: inválida.
5. Lectura con `∅` antes de una asignación posterior: inválida.

### E. Evidencia de ejecución

- Compilación: `Debug|x64`, 0 errores y 0 advertencias.
- Pruebas heredadas: 42.
- Pruebas nuevas: 11.
- Total: **53/53 aprobadas**.
- Diagnóstico real representativo:
  `[error][semantico] ...:1:14: Variable no inicializada: 'x'.`
- El ejecutable válido termina con código 2 porque falta RI y no deja `.lci`.

Comandos:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' '.\LCero-Compilador.slnx' /t:Build /m /p:Configuration=Debug /p:Platform=x64
& '.\bin\x64\Debug\LCero.Compiler.Tests.exe'
& '.\bin\x64\Debug\LCero.Compiler.exe'
```

### F. Fundamentación académica

Se demostraron, bajo las hipótesis indicadas, preservación de `I ⊆ D`,
monotonía de las transferencias y corrección conservadora respecto de los
caminos modelados. Las pruebas verifican la implementación sobre casos finitos,
pero no sustituyen esas demostraciones ni prueban ausencia universal de defectos.

La dependencia entre nombres distantes y caminos de control excede lo que
aporta por sí sola la derivación libre de contexto. Los LBA ilustran la memoria
lineal disponible para restricciones tipo 1 bajo una codificación fijada, pero
la tabla de símbolos no se identifica con un LBA y esta implementación concreta
no afirma una cota lineal debido a copias y trazas.

### G. Estado del proyecto

Quedaron implementados léxico, sintaxis, AST, tabla global y análisis semántico
con asignación definitiva. Permanecen pendientes RI, optimización, registros,
emisión y VM. Para el Módulo 4 quedan disponibles el AST por referencia, la tabla
de símbolos, el estado final y las trazas. `LCERO_IR 1` no fue modificado y se
confirmó que no se genera ningún `.lci`.
