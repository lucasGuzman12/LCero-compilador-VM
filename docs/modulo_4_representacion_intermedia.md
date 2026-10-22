# Módulo 4 — Representación intermedia y backend de referencia

## 1. Objetivo y arquitectura

Este módulo completa el Hito II, Fase 4. El compilador deja de finalizar en
`NotImplemented` y aplica la cadena:

```text
AST validado -> RI interna tipada -> backend de referencia ->
validador LCERO_IR 1 -> publicación transaccional
```

El AST conserva la estructura del programa fuente. La RI expresa el cálculo
mediante tres direcciones y temporales virtuales ilimitados. El backend decide
una ubicación física deliberadamente conservadora sin contaminar al generador
con los cuatro registros de la VM. Esta separación permite sustituir el backend
por el asignador del Módulo 5 sin reconstruir el AST ni su traducción.

## 2. Modelo formal de la RI

Sean:

- `Var = {v0,...,v(V-1)}`, variables declaradas;
- `Temp = {t0,...,t(T-1)}`, temporales virtuales;
- `Lab = {L0,...,L(K-1)}`, etiquetas;
- `Z`, enteros matemáticos de precisión arbitraria;
- `Instr`, el conjunto de las 18 operaciones enumeradas abajo.

Una configuración abstracta es `C = (pc, rho, mu, O)`, donde `pc` identifica
la siguiente instrucción, `rho : Temp -> Z` es el ambiente de temporales,
`mu : Var -> Z` es la memoria de variables y `O in Z*` es la secuencia impresa.
La tabla de etiquetas `lambda : Lab -> pc` se obtiene de las definiciones del
programa y no forma parte del estado mutable.

### 2.1 Inventario de instrucciones

| Operación | Forma abstracta | Efecto |
|---|---|---|
| `Constant` | `t := CONST n` | `rho[t] := n` |
| `LoadVariable` | `t := LOAD v` | `rho[t] := mu[v]` |
| `StoreVariable` | `STORE v, t` | `mu[v] := rho[t]` |
| `Add` | `t := ADD a,b` | suma |
| `Subtract` | `t := SUB a,b` | resta |
| `Multiply` | `t := MUL a,b` | producto |
| `Divide` | `t := DIV a,b` | cociente truncado hacia cero |
| `CompareEqual` | `t := CMP_EQ a,b` | igualdad, `0` o `1` |
| `CompareNotEqual` | `t := CMP_NE a,b` | desigualdad, `0` o `1` |
| `CompareLess` | `t := CMP_LT a,b` | menor, `0` o `1` |
| `CompareLessEqual` | `t := CMP_LE a,b` | menor o igual, `0` o `1` |
| `CompareGreater` | `t := CMP_GT a,b` | mayor, `0` o `1` |
| `CompareGreaterEqual` | `t := CMP_GE a,b` | mayor o igual, `0` o `1` |
| `Jump` | `JMP L` | `pc := lambda(L)` |
| `JumpIfZero` | `JZ t,L` | salta si `rho[t] = 0` |
| `Label` | `LABEL L` | no modifica el estado |
| `Print` | `PRINT t` | agrega `rho[t]` a `O` |
| `Halt` | `HALT` | finaliza normalmente |

Salvo los saltos y `HALT`, cada operación avanza a la siguiente instrucción.
`DIV` con divisor cero produce el mismo error dinámico exigido a la VM. Los
literales se conservan como cadenas decimales canónicas, por lo que el
compilador no reduce el dominio `Z` a un tipo entero de la plataforma.

### 2.2 Invariantes

- Cada temporal creado tiene un identificador distinto en `[0,T)`.
- Cada variable tiene identificador y dirección en `[0,V)`.
- Cada etiqueta referenciada está definida exactamente una vez en `[0,K)`.
- `BinaryInstruction` solo contiene una operación aritmética y
  `ComparisonInstruction` solo una comparación.
- Toda RI generada termina en `Halt`.
- El backend comprueba estos invariantes antes de emitir texto.

## 3. Traducción estructural del AST

Se escribe `[[e]] = (C,t)` cuando traducir `e` produce la secuencia `C` y deja
el resultado en `t`. El recorrido de hijos es de izquierda a derecha y sigue el
AST; no vuelve a analizar tokens.

- Literal `n`: temporal nuevo `t`; `t := CONST canon(n)`.
- Variable `x`: temporal nuevo `t`; `t := LOAD id(x)`.
- Negación `-e`: `[[e]]=(C,a)`; emite `z := CONST 0`, `t := SUB z,a`.
- Binaria `e1 op e2`: traduce `e1`, después `e2`, y emite
  `t := OP t1,t2`, para `OP` en `ADD,SUB,MUL,DIV`.
- Condición `e1 rel e2`: igual esquema con una de las seis `CMP_*`.
- Asignación `x=e`: traduce `e` y emite `STORE id(x),t`.
- Impresión `print(e)`: traduce `e` y emite `PRINT t`.
- Secuencia: concatena en orden las traducciones de sus sentencias.

El menos unario genera primero el cero y luego el operando; ambos órdenes son
observacionalmente equivalentes hoy porque las expresiones no tienen efectos,
pero la decisión queda explícita y determinista.

### 3.1 Flujo de control

Para un `if (c) A`, con etiqueta fresca `Lf`:

```text
[[c]]; JZ tc,Lf; [[A]]; LABEL Lf
```

Para `if (c) A else B`, con `Le` y `Lf` frescas:

```text
[[c]]; JZ tc,Le; [[A]]; JMP Lf; LABEL Le; [[B]]; LABEL Lf
```

Para `while (c) A`, con `Li` y `Lf` frescas:

```text
LABEL Li; [[c]]; JZ tc,Lf; [[A]]; JMP Li; LABEL Lf
```

La condición queda después de `Li`; por ello el salto de retorno la reevalúa en
cada iteración. Un contador único produce nombres deterministas `L0`, `L1`, etc.

## 4. Corrección de la traducción

Para expresiones, la propiedad buscada es:

```text
<e,mu> => v  y  [[e]] = (C,t)
implica que ejecutar C desde una rho compatible termina con rho[t] = v.
```

Demostración por inducción estructural:

1. En un literal, `CONST` instala exactamente su valor decimal; en una variable,
   `LOAD` copia `mu[x]`. Son los casos base.
2. En `-e`, la hipótesis inductiva deja el valor de `e` en `a`; `SUB 0,a`
   produce su inverso aditivo.
3. En `e1 op e2`, las hipótesis dejan ambos valores en temporales diferentes.
   La instrucción de tres direcciones lee las dos fuentes antes de escribir el
   destino y aplica la misma operación. Para `DIV`, tanto RI como VM truncan
   hacia cero y fallan si el segundo valor es cero.
4. Las comparaciones usan el mismo argumento y codifican verdad como `1` y
   falsedad como `0`.

Por inducción sobre sentencias, `STORE` preserva el estado salvo la variable
asignada y `PRINT` preserva memoria agregando exactamente un valor a `O`. La
concatenación preserva orden. Los esquemas de saltos seleccionan exactamente la
rama cuya condición corresponde, y el esquema del `while` repite prueba y
cuerpo hasta obtener cero. Esto justifica los constructores existentes; no se
afirma una prueba mecanizada ni propiedades sobre extensiones futuras.

## 5. Variables, memoria y backend de referencia

Las declaraciones del AST reciben direcciones por posición:
`address(vi)=i`. No se usa el orden alfabético de la tabla de símbolos. El
backend reserva un slot adicional por temporal:

```text
slot(vi) = i
slot(tj) = V + j
MEM = V + T
```

La materialización usa `R0` y `R1` para fuentes y `R2` para resultados; `R3`
permanece disponible. Por ejemplo, `t2 := ADD t0,t1` se emite como:

```text
LOAD R0, slot(t0)
LOAD R1, slot(t1)
ADD R2, R0, R1
STORE slot(t2), R2
```

Una constante se carga en `R0` y se guarda en su slot. `LOAD`/`STORE` de
variables también pasan por `R0`; `PRINT` carga su temporal en `R0`. Este
esquema genera tráfico redundante intencionalmente, pero no inventa registros,
mantiene valores vivos y deja que la VM realice aritmética de precisión
arbitraria y sus errores dinámicos.

## 6. Compatibilidad y publicación

`LciValidator` analiza el texto generado antes de tocar el destino final:
encabezado y `MEM`, ASCII, mnemónicas y aridades, registros `R0..R3`, enteros
canónicos, slots dentro de rango, etiquetas válidas/únicas/resueltas y presencia
de `HALT`. No ejecuta el programa ni sustituye a la VM.

`OutputArtifact` elimina primero la salida antigua, escribe un temporal en el
mismo directorio y lo renombra solo luego de completar todos los análisis, la
RI, el backend y la validación. Una compilación válida devuelve código 0; el
compilador no ejecuta el `.lci`.

Ejemplo para `print(5);`:

```text
RI: t0 := CONST 5; PRINT t0; HALT

LCERO_IR 1
MEM 1
CONST R0, 5
STORE 0, R0
LOAD R0, 0
PRINT R0
HALT
```

## 7. Complejidad

Sean `N` los nodos del AST, `B` la suma de longitudes de literales y nombres,
`I` las instrucciones RI, `V` las variables, `T` los temporales y `K` las
etiquetas. El recorrido crea una cantidad constante de instrucciones por nodo,
de modo que `I,T,K = O(N)`. La búsqueda de variables en `std::map` cuesta
`O(log V)`; el tiempo del generador es `O(N log V + B)` y su espacio
`O(I + V + B)`. El backend valida etiquetas con `std::set`, por lo que cuesta
`O(I log K + B)` y emite a lo sumo cuatro instrucciones físicas por instrucción
RI: tamaño y tiempo de emisión `O(I + B)`. Estas cotas son de compilación, no de
ejecución del bucle generado.

## 8. Preparación para Chaitin y limitaciones

`VirtualTemporary`, operandos tipados e instrucciones independientes de
registros constituyen la entrada natural para vivacidad, grafo de interferencia
y coloreo. `ReferenceBackend` es reemplazable y su política de slots no forma
parte de `Generator`.

Todavía no hay optimizaciones, análisis de vivacidad, Chaitin, coloreo ni
spilling selectivo. Todos los temporales se guardan en memoria. La VM pertenece
a otro repositorio: en este módulo se validaron forma y estructura del contrato,
pero no se afirma haber ejecutado los vectores con la VM independiente.

## AUDITORÍA DEL MÓDULO 4 — INFORMACIÓN PARA REVISIÓN EXTERNA

### A. Arquitectura

Se crearon `include/lcero/ir/{ir,generator,lci_validator,reference_backend}.hpp`
y sus cuatro implementaciones en `src/ir/`. Se modificaron `compiler.hpp`,
`compiler.cpp`, `main.cpp`, ambos proyectos de Visual Studio y las pruebas. La
dependencia es unidireccional: AST/tabla semántica -> `Generator` -> `Program`
RI -> `ReferenceBackend` -> texto -> `LciValidator` -> `OutputArtifact`. El
futuro optimizador puede consumir y producir `ir::Program` antes del backend.

### B. RI formal

La RI contiene 18 opcodes: tres de memoria/constante, cuatro aritméticos, seis
comparadores, dos saltos, etiqueta, impresión y finalización. Sus operandos son
`VariableId`, `VirtualTemporary`, `LabelId` y decimal canónico, no cadenas de
instrucciones. Estado, reglas e invariantes están definidos en las secciones 2
y 2.2. El backend vuelve a comprobar rangos, categorías, etiquetas y `HALT`.

### C. Traducción

La correspondencia completa de nodos está en la sección 3 y los esquemas de
control se dan literalmente en 3.1. Los comparadores se mapean uno a uno a
`CompareEqual`, `CompareNotEqual`, `CompareLess`, `CompareLessEqual`,
`CompareGreater` y `CompareGreaterEqual`. La evaluación es izquierda-derecha.
La división por cero no se pliega ni se oculta: permanece como `DIV` para que
ocurra el error dinámico contractual.

### D. Memoria y registros

Las variables ocupan `0..V-1` por orden de declaración y los temporales
`V..V+T-1`. Hay exactamente cuatro registros contractuales; el backend usa
`R0/R1/R2` como trabajo y nunca emite otro. La sección 5 muestra la bajada de
una operación. Chaitin podrá reemplazar ubicaciones de temporales y conservar
la misma RI; el spilling selectivo está pendiente.

### E. Contrato de la VM

La versión es `LCERO_IR 1`. El backend puede emitir `CONST`, `LOAD`, `STORE`,
los cuatro operadores, las seis comparaciones, `JMP`, `JZ`, `LABEL`, `PRINT` y
`HALT`; `MOV` está validado aunque el backend conservador no lo necesita. Las
firmas completas están en `contract/RI-v1.md`, y la sección 6 contiene un `.lci`
completo. Se validan sintaxis, recursos, rangos y referencias. Falta ejecutar
los vectores de `contract/` con la VM independiente cuando esté disponible.

### F. Pruebas

Se conservaron las 53 pruebas y se agregaron 7: estructura de expresiones y
direcciones, literal grande, comparadores/control, recursos físicos, validador
estricto, rechazo de RI inconsistente y error de escritura. También se actualizó la prueba integrada
para comprobar publicación exacta y las pruebas heredadas comprueban que fallos
léxicos, sintácticos y semánticos invalidan salidas antiguas. Resultado final:
60/60; Debug|x64, 0 errores y 0 advertencias.

Comandos de reproducción:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' '.\LCero-Compilador.slnx' /t:Build /m /p:Configuration=Debug /p:Platform=x64
.\bin\x64\Debug\LCero.Compiler.Tests.exe
.\bin\x64\Debug\LCero.Compiler.exe
```

### G. Evidencia matemática

Las reglas, el invariante de resultado y la inducción estructural aparecen en
las secciones 2–4; la complejidad, en la 7. La preservación se argumentó para
los nodos implementados y se verificó experimentalmente en traducciones
representativas. No existe aún una prueba mecanizada ni una integración
ejecutada con la VM externa; ambas afirmaciones quedan deliberadamente fuera.

### H. Limitaciones y próximos pasos

No se implementaron optimización, vivacidad, Chaitin, coloreo o spilling
selectivo. El principal costo es `MEM=V+T` y cargas/guardados redundantes. Los
riesgos de integración son diferencias en el cargador de la VM o en enteros
grandes; los vectores contractuales permiten detectarlas sin cambiar el
contrato. El próximo módulo debe insertar análisis de vivacidad y asignación
entre `Generator` y el backend, conservando este backend como referencia.
