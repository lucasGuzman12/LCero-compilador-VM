# L-Cero — Módulo 2: análisis sintáctico LL(1) y AST

## Resultado

El compilador incorpora una gramática BNF ejecutable, su análisis formal hasta
punto fijo, una tabla predictiva LL(1), un parser descendente predictivo y un
árbol de sintaxis abstracta con propiedad explícita mediante `std::unique_ptr`.
La salida `.lci` continúa invalidándose al comenzar y no se publica: el pipeline
se detiene deliberadamente antes del análisis semántico y la generación.

La gramática canónica está definida una sola vez en `Grammar::lcero()`. De esa
misma colección de producciones se calculan NULLABLE, FIRST, FOLLOW, SELECT y la
tabla que consulta el parser. Los archivos de `docs/gramatica` son una
representación auditable de esos datos, no una segunda implementación.

Este trabajo completa el Hito I, Fase 2: transforma la sintaxis acordada en un
reconocedor predictivo y deja un AST estable como entrada del futuro Módulo 3.

## Definición formal y EBNF de origen

La gramática es `G = (V, T, P, S)`, con:

- `V`: los 20 no terminales enumerados en la BNF de la sección siguiente.
- `T`: `{KW_INT, KW_IF, KW_ELSE, KW_WHILE, KW_PRINT, ID, NUMBER, PLUS,
  MINUS, STAR, SLASH, EQ, NE, LT, GT, LE, GE, ASSIGN, LPAREN, RPAREN,
  LBRACE, RBRACE, SEMICOLON, EOF}`. Coinciden con los tokens emitidos por el
  lexer; `WHITESPACE` se reconoce pero no llega al parser.
- `P`: las 38 producciones numeradas de la sección siguiente.
- `S = Programa`.

La EBNF aprobada de partida es:

```ebnf
Programa    = { Declaracion }, Instruccion, { Instruccion } ;
Declaracion = "int", ID, ";" ;
Instruccion = Asignacion | Impresion | Condicional | Bucle ;
Asignacion  = ID, "=", Expresion, ";" ;
Impresion   = "print", "(", Expresion, ")", ";" ;
Condicional = "if", "(", Condicion, ")", Bloque, [ "else", Bloque ] ;
Bucle       = "while", "(", Condicion, ")", Bloque ;
Bloque      = "{", Instruccion, { Instruccion }, "}" ;
Condicion   = Expresion, Comparador, Expresion ;
Comparador  = "==" | "!=" | "<" | ">" | "<=" | ">=" ;
Expresion   = Termino, { ("+" | "-"), Termino } ;
Termino     = Unario, { ("*" | "/"), Unario } ;
Unario      = "-", Unario | Primario ;
Primario    = NUMBER | ID | "(", Expresion, ")" ;
```

## Gramática adoptada

La misma BNF se conserva también en
[gramatica/lcero_bnf.txt](gramatica/lcero_bnf.txt). Consta de 20 no terminales,
24 terminales y 38 producciones:

```text
 1. Programa -> DeclSeq StmtSeq EOF
 2. DeclSeq -> Declaracion DeclSeq
 3. DeclSeq -> epsilon
 4. Declaracion -> KW_INT ID SEMICOLON
 5. StmtSeq -> Instruccion StmtSeqTail
 6. StmtSeqTail -> Instruccion StmtSeqTail
 7. StmtSeqTail -> epsilon
 8. Instruccion -> Asignacion
 9. Instruccion -> Impresion
10. Instruccion -> Condicional
11. Instruccion -> Bucle
12. Asignacion -> ID ASSIGN Expresion SEMICOLON
13. Impresion -> KW_PRINT LPAREN Expresion RPAREN SEMICOLON
14. Condicional -> KW_IF LPAREN Condicion RPAREN Bloque ElseOpt
15. ElseOpt -> KW_ELSE Bloque
16. ElseOpt -> epsilon
17. Bucle -> KW_WHILE LPAREN Condicion RPAREN Bloque
18. Bloque -> LBRACE StmtSeq RBRACE
19. Condicion -> Expresion Comparador Expresion
20. Comparador -> EQ
21. Comparador -> NE
22. Comparador -> LT
23. Comparador -> GT
24. Comparador -> LE
25. Comparador -> GE
26. Expresion -> Termino ExprTail
27. ExprTail -> PLUS Termino ExprTail
28. ExprTail -> MINUS Termino ExprTail
29. ExprTail -> epsilon
30. Termino -> Unario TermTail
31. TermTail -> STAR Unario TermTail
32. TermTail -> SLASH Unario TermTail
33. TermTail -> epsilon
34. Unario -> MINUS Unario
35. Unario -> Primario
36. Primario -> NUMBER
37. Primario -> ID
38. Primario -> LPAREN Expresion RPAREN
```

Las producciones que derivan directamente `epsilon` son 3, 7, 16, 29 y 33.
Las decisiones más importantes son:

- `DeclSeq` permite cero o más declaraciones, siempre antes de las instrucciones.
- `StmtSeq` exige una instrucción; por eso el programa y cada bloque son no vacíos.
- `ElseOpt` resuelve el `else` opcional sin ambigüedad porque las ramas usan bloques.
- `ExprTail` y `TermTail` eliminan la recursión izquierda conservando precedencia.
- `Unario -> MINUS Unario` admite negación encadenada.
- `Condicion` exige exactamente una comparación entre dos expresiones aritméticas.
- `EOF` forma parte de `Programa`, de modo que no pueden quedar tokens sin consumir.

La conversión conserva el lenguaje: cada repetición EBNF se reemplaza por un no
terminal de lista con caso recursivo y caso `epsilon`; la opción de `else` se
reemplaza por `ElseOpt`; y las repeticiones de operadores se reemplazan por las
colas `ExprTail` y `TermTail`. No se agrega ni elimina ningún terminal. La
separación `StmtSeq`/`StmtSeqTail` mantiene la cardinalidad mínima uno de las
instrucciones, mientras `DeclSeq` mantiene cardinalidad mínima cero. La
recursión izquierda aritmética se evita con las colas; el plegado del AST
recupera la asociatividad izquierda que expresa la EBNF.

Ejemplo de derivación izquierda para `print(1);`:

```text
Programa
=> DeclSeq StmtSeq EOF
=> StmtSeq EOF
=> Instruccion StmtSeqTail EOF
=> Impresion StmtSeqTail EOF
=> KW_PRINT LPAREN Expresion RPAREN SEMICOLON StmtSeqTail EOF
=> KW_PRINT LPAREN Termino ExprTail RPAREN SEMICOLON StmtSeqTail EOF
=> KW_PRINT LPAREN Unario TermTail ExprTail RPAREN SEMICOLON StmtSeqTail EOF
=> KW_PRINT LPAREN Primario TermTail ExprTail RPAREN SEMICOLON StmtSeqTail EOF
=> KW_PRINT LPAREN NUMBER TermTail ExprTail RPAREN SEMICOLON StmtSeqTail EOF
=> KW_PRINT LPAREN NUMBER RPAREN SEMICOLON EOF
```

La expresión `10 - 3 - 2 * 4` se reconoce con las colas LL(1), pero durante la
construcción del AST se pliega a izquierda:

```text
Subtract(Subtract(10, 3), Multiply(2, 4))
```

Esto conserva asociatividad izquierda y la precedencia multiplicativa sin
reintroducir recursión izquierda en la gramática.

## Construcción formal

`GrammarAnalysis` aplica iteraciones monótonas sobre conjuntos ordenados:

1. NULLABLE agrega un lado izquierdo cuando toda su parte derecha es anulable.
2. FIRST agrega el primer terminal posible de cada secuencia y continúa sobre
   símbolos anulables.
3. FOLLOW inicia con `EOF` en `Programa`; para cada no terminal agrega FIRST del
   sufijo y, si ese sufijo es anulable, propaga FOLLOW del lado izquierdo.
4. SELECT de `A -> alfa` es FIRST(alfa), unido a FOLLOW(A) cuando alfa es anulable.
5. Cada par `(A, terminal)` de SELECT se inserta en la tabla predictiva; una
   segunda producción en la misma celda se registra como conflicto.

Incluyendo la pasada final que confirma que no hubo cambios, el cálculo actual
converge en 2 pasadas de NULLABLE, 6 de FIRST y 3 de FOLLOW. Los resultados
completos son:

- [gramatica/first.csv](gramatica/first.csv)
- [gramatica/follow.csv](gramatica/follow.csv)
- [gramatica/select.csv](gramatica/select.csv)
- [gramatica/tabla_ll1.csv](gramatica/tabla_ll1.csv)

En NULLABLE, la primera pasada incorpora directamente los lados izquierdos de
3, 7, 16, 29 y 33; la segunda confirma el punto fijo. En FIRST, la primera
pasada incorpora los terminales iniciales visibles y las pasadas siguientes
propagan, entre otras, la cadena `Primario -> Unario -> Termino -> Expresion ->
Condicion`; la sexta no cambia conjuntos. En FOLLOW, la primera pasada agrega
los terminales de sufijos inmediatos y propaga desde `Programa`, `Bloque` y las
reglas aritméticas; la segunda completa las dependencias indirectas y la tercera
confirma la convergencia.

Tabla completa de NULLABLE y FIRST:

```csv
nonterminal,nullable,FIRST
Programa,no,KW_INT|KW_IF|KW_WHILE|KW_PRINT|ID
DeclSeq,yes,KW_INT
Declaracion,no,KW_INT
StmtSeq,no,KW_IF|KW_WHILE|KW_PRINT|ID
StmtSeqTail,yes,KW_IF|KW_WHILE|KW_PRINT|ID
Instruccion,no,KW_IF|KW_WHILE|KW_PRINT|ID
Asignacion,no,ID
Impresion,no,KW_PRINT
Condicional,no,KW_IF
ElseOpt,yes,KW_ELSE
Bucle,no,KW_WHILE
Bloque,no,LBRACE
Condicion,no,ID|NUMBER|MINUS|LPAREN
Comparador,no,EQ|NE|LT|GT|LE|GE
Expresion,no,ID|NUMBER|MINUS|LPAREN
ExprTail,yes,PLUS|MINUS
Termino,no,ID|NUMBER|MINUS|LPAREN
TermTail,yes,STAR|SLASH
Unario,no,ID|NUMBER|MINUS|LPAREN
Primario,no,ID|NUMBER|LPAREN
```

Tabla completa de FOLLOW:

```csv
nonterminal,FOLLOW
Programa,EOF
DeclSeq,KW_IF|KW_WHILE|KW_PRINT|ID
Declaracion,KW_INT|KW_IF|KW_WHILE|KW_PRINT|ID
StmtSeq,RBRACE|EOF
StmtSeqTail,RBRACE|EOF
Instruccion,KW_IF|KW_WHILE|KW_PRINT|ID|RBRACE|EOF
Asignacion,KW_IF|KW_WHILE|KW_PRINT|ID|RBRACE|EOF
Impresion,KW_IF|KW_WHILE|KW_PRINT|ID|RBRACE|EOF
Condicional,KW_IF|KW_WHILE|KW_PRINT|ID|RBRACE|EOF
ElseOpt,KW_IF|KW_WHILE|KW_PRINT|ID|RBRACE|EOF
Bucle,KW_IF|KW_WHILE|KW_PRINT|ID|RBRACE|EOF
Bloque,KW_IF|KW_ELSE|KW_WHILE|KW_PRINT|ID|RBRACE|EOF
Condicion,RPAREN
Comparador,ID|NUMBER|MINUS|LPAREN
Expresion,EQ|NE|LT|GT|LE|GE|RPAREN|SEMICOLON
ExprTail,EQ|NE|LT|GT|LE|GE|RPAREN|SEMICOLON
Termino,PLUS|MINUS|EQ|NE|LT|GT|LE|GE|RPAREN|SEMICOLON
TermTail,PLUS|MINUS|EQ|NE|LT|GT|LE|GE|RPAREN|SEMICOLON
Unario,PLUS|MINUS|STAR|SLASH|EQ|NE|LT|GT|LE|GE|RPAREN|SEMICOLON
Primario,PLUS|MINUS|STAR|SLASH|EQ|NE|LT|GT|LE|GE|RPAREN|SEMICOLON
```

SELECT completo por producción:

```csv
1,KW_INT|KW_IF|KW_WHILE|KW_PRINT|ID
2,KW_INT
3,KW_IF|KW_WHILE|KW_PRINT|ID
4,KW_INT
5,KW_IF|KW_WHILE|KW_PRINT|ID
6,KW_IF|KW_WHILE|KW_PRINT|ID
7,RBRACE|EOF
8,ID
9,KW_PRINT
10,KW_IF
11,KW_WHILE
12,ID
13,KW_PRINT
14,KW_IF
15,KW_ELSE
16,KW_IF|KW_WHILE|KW_PRINT|ID|RBRACE|EOF
17,KW_WHILE
18,LBRACE
19,ID|NUMBER|MINUS|LPAREN
20,EQ
21,NE
22,LT
23,GT
24,LE
25,GE
26,ID|NUMBER|MINUS|LPAREN
27,PLUS
28,MINUS
29,EQ|NE|LT|GT|LE|GE|RPAREN|SEMICOLON
30,ID|NUMBER|MINUS|LPAREN
31,STAR
32,SLASH
33,PLUS|MINUS|EQ|NE|LT|GT|LE|GE|RPAREN|SEMICOLON
34,MINUS
35,ID|NUMBER|LPAREN
36,NUMBER
37,ID
38,LPAREN
```

Tabla predictiva completa, compactada por fila como `lookahead:producción`:

```text
Programa: KW_INT:1, KW_IF:1, KW_WHILE:1, KW_PRINT:1, ID:1
DeclSeq: KW_INT:2, KW_IF:3, KW_WHILE:3, KW_PRINT:3, ID:3
Declaracion: KW_INT:4
StmtSeq: KW_IF:5, KW_WHILE:5, KW_PRINT:5, ID:5
StmtSeqTail: KW_IF:6, KW_WHILE:6, KW_PRINT:6, ID:6, RBRACE:7, EOF:7
Instruccion: KW_IF:10, KW_WHILE:11, KW_PRINT:9, ID:8
Asignacion: ID:12
Impresion: KW_PRINT:13
Condicional: KW_IF:14
ElseOpt: KW_IF:16, KW_ELSE:15, KW_WHILE:16, KW_PRINT:16, ID:16, RBRACE:16, EOF:16
Bucle: KW_WHILE:17
Bloque: LBRACE:18
Condicion: ID:19, NUMBER:19, MINUS:19, LPAREN:19
Comparador: EQ:20, NE:21, LT:22, GT:23, LE:24, GE:25
Expresion: ID:26, NUMBER:26, MINUS:26, LPAREN:26
ExprTail: PLUS:27, MINUS:28, EQ:29, NE:29, LT:29, GT:29, LE:29, GE:29, RPAREN:29, SEMICOLON:29
Termino: ID:30, NUMBER:30, MINUS:30, LPAREN:30
TermTail: PLUS:33, MINUS:33, STAR:31, SLASH:32, EQ:33, NE:33, LT:33, GT:33, LE:33, GE:33, RPAREN:33, SEMICOLON:33
Unario: ID:35, NUMBER:35, MINUS:34, LPAREN:35
Primario: ID:37, NUMBER:36, LPAREN:38
```

El conjunto NULLABLE final es `{DeclSeq, StmtSeqTail, ElseOpt, ExprTail,
TermTail}`. Todas las parejas de alternativas de un mismo no terminal tienen
SELECT disjuntos. La tabla contiene 84 celdas definidas y cero conflictos; por
lo tanto, la gramática es LL(1).

La disjunción puede verificarse por familias: las cuatro clases de instrucción
comienzan respectivamente con `ID`, `KW_PRINT`, `KW_IF` y `KW_WHILE`; los seis
comparadores tienen tokens distintos; las alternativas no vacías de las colas
aritméticas comienzan con su operador y sus alternativas `epsilon` seleccionan
FOLLOW, que no contiene esos operadores; `Unario` separa `MINUS` de
`{ID, NUMBER, LPAREN}`. Para `ElseOpt`, la producción 15 selecciona únicamente
`KW_ELSE`, mientras la 16 selecciona
`{KW_IF, KW_WHILE, KW_PRINT, ID, RBRACE, EOF}`. Las llaves obligatorias cierran
la rama interna antes de decidir el `else`, eliminando el caso de *dangling
else*. Como para cada par `(no terminal, lookahead)` existe a lo sumo una
producción, toda derivación izquierda está determinada; esto prueba la ausencia
de ambigüedad de esta gramática, además de la propiedad LL(1).

El reporte se puede regenerar directamente desde las estructuras ejecutables:

```powershell
bin\x64\Debug\LCero.Compiler.Tests.exe --grammar-report
```

## Correspondencia entre tabla y parser

Cada procedimiento del parser comienza consultando
`GrammarAnalysis::production_for(no_terminal, lookahead)`. No existe una tabla
paralela escrita a mano. La correspondencia principal es:

| No terminal | Rutina | Producciones |
|---|---|---:|
| `Programa` | `parse_program` | 1 |
| `DeclSeq`, `Declaracion` | `parse_declaration_sequence`, `parse_declaration` | 2–4 |
| `StmtSeq`, `StmtSeqTail`, `Instruccion` | rutinas de secuencia y despacho | 5–11 |
| `Asignacion`, `Impresion` | rutinas homónimas | 12–13 |
| `Condicional`, `ElseOpt`, `Bucle`, `Bloque` | rutinas de control | 14–18 |
| `Condicion`, `Comparador` | rutinas de condición | 19–25 |
| `Expresion`, `ExprTail` | `parse_expression` | 26–29 |
| `Termino`, `TermTail` | `parse_term` | 30–33 |
| `Unario`, `Primario` | rutinas homónimas | 34–38 |

El parser consume el `EOF` de la producción inicial. Ante una celda vacía o un
terminal distinto del esperado, emite un único diagnóstico y abandona el
análisis; no intenta recuperación en este módulo.

## Árbol de sintaxis abstracta

El AST omite puntuación y no terminales auxiliares. Conserva únicamente la
estructura útil para las fases posteriores:

```text
Program
├── Declaration[]
└── Statement[]
    ├── AssignmentStatement(name, expression)
    ├── PrintStatement(expression)
    ├── IfStatement(condition, then_block, else_block?)
    └── WhileStatement(condition, body)

Expression
├── IntegerLiteral(text)
├── VariableExpression(name)
├── UnaryExpression(operation, operand)
└── BinaryExpression(operation, left, right)
```

AST completo para `int x; x = 2 + 3 * 4; print(x);`:

```text
Program
├── declarations
│   └── Declaration(name="x")
└── statements
    ├── AssignmentStatement(name="x")
    │   └── BinaryExpression(Add)
    │       ├── IntegerLiteral("2")
    │       └── BinaryExpression(Multiply)
    │           ├── IntegerLiteral("3")
    │           └── IntegerLiteral("4")
    └── PrintStatement
        └── VariableExpression(name="x")
```

AST completo para
`if (x < 10) { print(x); } else { while (x != 0) { x = x - 1; } }`:

```text
Program
└── statements
    └── IfStatement
        ├── Condition(Less)
        │   ├── VariableExpression("x")
        │   └── IntegerLiteral("10")
        ├── then Block
        │   └── PrintStatement
        │       └── VariableExpression("x")
        └── else Block
            └── WhileStatement
                ├── Condition(NotEqual)
                │   ├── VariableExpression("x")
                │   └── IntegerLiteral("0")
                └── Block
                    └── AssignmentStatement(name="x")
                        └── BinaryExpression(Subtract)
                            ├── VariableExpression("x")
                            └── IntegerLiteral("1")
```

`Condition` guarda el operador relacional y sus dos expresiones; `Block` guarda
las instrucciones en orden. Cada nodo tiene línea, columna y desplazamiento de
origen. Los enteros se conservan como texto decimal, por lo que el parser no
introduce desbordamientos antes del análisis semántico. Los hijos polimórficos
usan `std::unique_ptr`, de modo que no hay propiedad compartida ni liberaciones
manuales.

## Diagnósticos e integración

Los errores usan la categoría `Syntactic`, la ruta del fuente y la posición del
lookahead. El mensaje identifica el token encontrado y el conjunto de tokens
esperados derivado de la fila correspondiente de la tabla. Ejemplo conceptual:

```text
Token inesperado EOF; se esperaba SEMICOLON.
```

`Compiler::run` ejecuta lectura, léxico y parser en ese orden. Un error léxico
impide invocar el parser; un error sintáctico devuelve `Failed`; un programa
sintácticamente válido devuelve por ahora `NotImplemented`, registra cantidades
de declaraciones e instrucciones superiores y explica que falta la etapa
semántica. En ningún caso del módulo se genera un `.lci` engañoso.

## Complejidad preliminar

Sea `n` la cantidad de tokens emitidos, incluido `EOF`, y considérese fija la
gramática. Cada decisión consulta una fila finita de la tabla y cada token se
consume una sola vez; por ello el reconocimiento y la construcción estructural
cumplen `T(n) = Θ(n)`. Aunque la implementación usa `std::map`, sus tamaños están
acotados por los 20 no terminales y 24 terminales, por lo que cada consulta es
constante respecto de `n`.

El AST contiene a lo sumo una cantidad lineal de nodos y ocupa `Θ(n)` memoria.
La pila de llamadas es `O(h)`, donde `h <= n` es la profundidad de paréntesis,
negaciones o bloques anidados; las colas de suma y producto se procesan con
bucles y no aumentan esa pila por cada operador. Copiar nombres y literales
cuesta además `Θ(L)`, donde `L` es la suma de sus longitudes; conservar los
literales como texto evita que ese costo dependa de un tipo entero limitado.
El cálculo de FIRST/FOLLOW/SELECT ocurre al construir el parser y depende del
tamaño de la gramática, no del programa concreto; para esta gramática fija es
un costo constante frente a `n`.

## Verificación

La suite automatizada cubre, además de las 30 pruebas previas:

- programas mínimos, declaraciones, asignaciones, impresión y anidamiento;
- los seis comparadores, las cuatro operaciones y el menos unario;
- precedencia, asociatividad izquierda, paréntesis y literales grandes;
- `if` con y sin `else`, `while`, bloques y orden de instrucciones;
- fuente vacío, programa sólo con declaraciones, bloques vacíos, declaraciones
  tardías, inicialización en declaración, delimitadores ausentes y expresiones
  incompletas;
- categoría y posición del primer error sintáctico;
- inventario de la gramática y conjunto NULLABLE;
- resultados representativos de FIRST y FOLLOW;
- SELECT no vacíos, disjuntos por no terminal y correspondencia exacta con cada
  celda de la tabla predictiva;
- igualdad byte a byte entre los CSV documentados y los conjuntos/tablas
  regenerados desde la gramática ejecutable;
- integración del pipeline y ausencia de `.lci` ante éxito parcial o error.

Comando usado:

```powershell
bin\x64\Debug\LCero.Compiler.Tests.exe
```

Resultado de referencia: `42/42 pruebas aprobadas`.

## AUDITORÍA DEL MÓDULO 2 — INFORMACIÓN PARA REVISIÓN EXTERNA

### A. Gramática

La lista íntegra de 38 producciones BNF numeradas aparece en “Gramática
adoptada” dentro de este mismo documento. Hay 20 no terminales, 24 terminales y
38 producciones; el símbolo inicial es `Programa`. Las producciones epsilon son
3 (`DeclSeq`), 7 (`StmtSeqTail`), 16 (`ElseOpt`), 29 (`ExprTail`) y 33
(`TermTail`). La EBNF original y la justificación constructiva de equivalencia
también están reproducidas arriba.

### B. FIRST, FOLLOW, SELECT y LL(1)

Las tablas completas de NULLABLE/FIRST, FOLLOW, SELECT y la tabla predictiva
compactada están incluidas en “Construcción formal”. Los CSV complementarios
permiten procesarlas automáticamente. Resultados auditables:

- NULLABLE converge en 2 pasadas, FIRST en 6 y FOLLOW en 3, contando la pasada
  final sin cambios.
- Las 38 producciones tienen SELECT no vacío.
- Cada pareja de alternativas con el mismo lado izquierdo tiene intersección
  SELECT vacía.
- La tabla tiene 84 celdas definidas, ninguna con más de una producción.
- Conflictos detectados: **0**.
- Conclusión formal: la gramática es LL(1) y, por determinismo de la derivación
  izquierda, no ambigua.

### C. Correspondencia implementación–teoría

- `src/parser/grammar.cpp` contiene las 38 producciones y los algoritmos de
  NULLABLE, FIRST, FOLLOW, SELECT y tabla.
- `include/lcero/parser/grammar.hpp` expone conjuntos, tabla, conflictos y
  contadores de pasadas.
- `src/parser/parser.cpp` reconoce todos los no terminales mediante las rutinas
  mapeadas en “Correspondencia entre tabla y parser”.
- `select_production` consulta directamente `M[A, lookahead]`; `consume` valida
  terminales. No hay decisiones sintácticas independientes de la gramática.
- `ExprTail` y `TermTail` eliminan la recursión izquierda. `parse_expression` y
  `parse_term` pliegan iterativamente a izquierda para construir el AST.
- `ElseOpt` se decide con `KW_ELSE` frente a FOLLOW; los bloques obligatorios
  impiden el *dangling else*.
- `src/compiler.cpp` entrega al parser exactamente los tokens del lexer y nunca
  vuelve a leer ni tokenizar el fuente.

### D. AST

La jerarquía exacta es `Node` como base; `Program`, `Declaration`, `Block` y
`Condition`; `Statement` con `AssignmentStatement`, `PrintStatement`,
`IfStatement` y `WhileStatement`; y `Expression` con `IntegerLiteral`,
`VariableExpression`, `UnaryExpression` y `BinaryExpression`.

`Program` almacena declaraciones e instrucciones ordenadas; las declaraciones y
referencias guardan nombres; el literal guarda texto decimal exacto; operadores
unarios, binarios y relacionales usan enumeraciones; bloques guardan secuencias;
y todos los nodos guardan línea, columna y desplazamiento. La propiedad de hijos
polimórficos es exclusiva mediante `std::unique_ptr`; los vectores poseen sus
elementos y RAII libera todo el árbol. Dos AST completos se muestran en la
sección de diseño del AST.

### E. Evidencia verificable

- Pruebas heredadas: **30**.
- Pruebas nuevas: **12**.
- Resultado total: **42/42 aprobadas**.
- Compilación `Debug|x64`: correcta, **0 advertencias y 0 errores**.
- Ejecución predeterminada: léxico y sintaxis completados, código de salida 2
  por fases futuras y ausencia confirmada de `LCIs/programa.lci`.
- Casos relevantes: cuatro operaciones, seis comparadores, precedencia,
  asociatividad, negación, literal arbitrariamente grande, bloques anidados,
  `if` con/sin `else`, `while`, fuente vacío, bloque vacío, declaración tardía,
  inicialización prohibida, delimitadores ausentes, operadores consecutivos y
  tokens sobrantes.
- Archivos formales: `docs/gramatica/lcero_bnf.txt`, `first.csv`, `follow.csv`,
  `select.csv` y `tabla_ll1.csv`.
- Una prueba reconstruye los cuatro CSV desde `Grammar::lcero()` y exige igualdad
  byte a byte con lo documentado.

Comandos de reproducción:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' '.\LCero-Compilador.slnx' /t:Build /m /p:Configuration=Debug /p:Platform=x64
& '.\bin\x64\Debug\LCero.Compiler.Tests.exe'
& '.\bin\x64\Debug\LCero.Compiler.Tests.exe' --grammar-report
& '.\bin\x64\Debug\LCero.Compiler.exe'
```

### F. Alcance de las conclusiones y limitaciones

Está demostrado algorítmicamente sobre la gramática finita completa que los
conjuntos alcanzan punto fijo, que SELECT es disjunto por no terminal y que la
tabla carece de conflictos. La equivalencia EBNF–BNF se justifica por las
transformaciones estándar de repetición/opción y por el plegado izquierdo del
AST. La corrección de implementaciones concretas, posiciones y formas del AST se
verifica mediante pruebas; esas pruebas no constituyen por sí solas una
demostración universal de ausencia de defectos de programación.

Decisiones técnicas: literal decimal textual, propiedad exclusiva con RAII,
primer error sintáctico solamente y tabla calculada en tiempo de construcción.
Conviene revisar en módulos futuros el límite práctico de la pila ante
anidamientos adversarialmente profundos y conectar el `Program` resultante con
el analizador semántico sin duplicar propiedad.

Quedan fuera de alcance, de forma intencional, la tabla de símbolos, variables
duplicadas/no declaradas/no inicializadas, rangos enteros, RI, optimización,
publicación `.lci` y máquina virtual. Por ejemplo, `x = 5;` es correctamente
aceptado por sintaxis aunque el futuro análisis semántico deba rechazarlo si `x`
no fue declarada.

| Requisito observable | Evidencia | Estado |
|---|---|---|
| Declaraciones `int ID;` sólo al comienzo | producciones 1–4 y casos negativos | Cumple |
| Al menos una instrucción por programa y bloque | producciones 5 y 18; `StmtSeq` no anulable | Cumple |
| Asignación e impresión | producciones 12–13 y nodos AST específicos | Cumple |
| `if`/`else` y `while` con llaves | producciones 14–18 y nodos de control | Cumple |
| Seis comparadores | producciones 20–25 y prueba parametrizada | Cumple |
| Precedencia, paréntesis y menos unario | producciones 26–38 y pruebas estructurales del AST | Cumple |
| Consumo completo del archivo | `Programa -> ... EOF` | Cumple |
| Construcción y justificación LL(1) | algoritmos, CSV y cero conflictos | Cumple |
| Error con token esperado y posición | `report_expected` y prueba dedicada | Cumple |
| AST independiente de la puntuación | jerarquía en `include/lcero/ast/ast.hpp` | Cumple |
| No adelantar semántica ni generar `.lci` | estado `NotImplemented` posterior al parser | Cumple |
