# Semántica operacional observada de L-Cero

## Configuración e invariantes

La máquina se representa mediante `C=(P,pc,R,M,O,k)`:

- `P` es la secuencia inmutable de instrucciones ejecutables tipadas;
- `pc` es el índice 0-based de la próxima instrucción, salvo que `HALT` lo deje sobre sí mismo;
- `R` contiene cuatro valores opcionales `R0..R3` en el dominio entero `cpp_int`;
- `M` es memoria dispersa acotada lógicamente por `MEM n`;
- `O` es la secuencia ordenada de enteros producidos por `PRINT`;
- `k` cuenta solamente instrucciones ejecutables completadas.

Encabezado, `MEM` y `LABEL` son directivas de representación, no transiciones, por lo que no aparecen en `P` ni incrementan `k`. Cada `Instruction` conserva `source_line`; la posición en `P` es su índice ejecutable. La tabla `LABEL -> pc` se conserva para diagnóstico, aunque los saltos ya contienen destinos tipados.

Escribimos `C -> C'` cuando una instrucción completa una transición. Toda instrucción ordinaria completada incrementa `k` una vez. Antes de escribir un destino se leen todos sus operandos fuente; esto permite alias como `ADD R0,R0,R1` sin perder un valor prematuramente.

## Transiciones principales

Para abreviar, `R[r <- v]` y `M[m <- v]` indican actualización funcional: el resto de la estructura no cambia.

### CONST

Si `P[pc] = CONST Rd,n`:

```text
(P,pc,R,M,O,k) -> (P,pc+1,R[Rd <- n],M,O,k+1)
```

`n` ya es un `cpp_int` validado; la transición no vuelve a analizar texto decimal.

### LOAD

Si `P[pc] = LOAD Rd,m` y `M[m]=v`:

```text
(P,pc,R,M,O,k) -> (P,pc+1,R[Rd <- v],M,O,k+1)
```

Si `m` no fue inicializado, la instrucción falla dinámicamente y no completa la transición.

### STORE

Si `P[pc] = STORE m,Rs` y `R[Rs]=v`:

```text
(P,pc,R,M,O,k) -> (P,pc+1,R,M[m <- v],O,k+1)
```

Solo el slot escrito se materializa en la memoria dispersa. Un registro fuente ausente produce error dinámico.

### ADD

Si `P[pc] = ADD Rd,Ra,Rb`, `R[Ra]=a` y `R[Rb]=b`:

```text
(P,pc,R,M,O,k) -> (P,pc+1,R[Rd <- a+b],M,O,k+1)
```

`a` y `b` se copian antes de modificar `Rd`. `SUB` y `MUL` siguen la misma forma con su operación matemática.

### DIV

Si `P[pc] = DIV Rd,Ra,Rb`, `R[Ra]=a`, `R[Rb]=b` y `b != 0`:

```text
(P,pc,R,M,O,k) -> (P,pc+1,R[Rd <- trunc(a/b)],M,O,k+1)
```

`trunc` redondea hacia cero; por ejemplo, `-7/2=-3`. Si `b=0`, se produce `ERROR` sin escribir `Rd`, avanzar `pc` ni incrementar `k`.

### JMP

Si `P[pc] = JMP t`:

```text
(P,pc,R,M,O,k) -> (P,t,R,M,O,k+1)
```

No se aplica un incremento adicional a `t`.

### JZ

Si `P[pc] = JZ Rs,t` y `R[Rs]=v`:

```text
v = 0  : (P,pc,R,M,O,k) -> (P,t,R,M,O,k+1)
v != 0 : (P,pc,R,M,O,k) -> (P,pc+1,R,M,O,k+1)
```

Un registro condicional ausente produce error dinámico.

### PRINT

Si `P[pc] = PRINT Rs` y `R[Rs]=v`:

```text
(P,pc,R,M,O,k) -> (P,pc+1,R,M,O·v,k+1)
```

`O·v` agrega el valor al final. Al serializarlo, la CLI escribe su decimal exacto seguido por un byte LF. La secuencia ya acumulada se conserva ante `ERROR` o `LIMIT` posteriores.

### HALT

Si `P[pc] = HALT`:

```text
(P,pc,R,M,O,k,RUNNING) -> (P,pc,R,M,O,k+1,HALT)
```

`HALT` cuenta un paso y no avanza `pc`.

## Comparaciones y MOV

`MOV Rd,Rs` copia el valor previo de `Rs`. Los seis `CMP_*` leen ambos enteros y escriben exactamente el entero 1 si la relación es verdadera o 0 en caso contrario. Nunca escriben un booleano ajeno al dominio numérico de la VM.

## LIMIT y errores

Antes de ejecutar una instrucción existente, si hay presupuesto `N` y `k >= N`, la máquina termina en `LIMIT` sin ejecutar ni incrementar `k`. Por eso `N=0` limita antes de la primera instrucción, mientras que un `HALT` completado como paso N conserva `HALT`.

Si `pc=|P|`, no existe `P[pc]`: la ejecución termina en `ERROR` por caída sin `HALT`. Una etiqueta en EOF puede producir precisamente este caso dinámico aunque sea válida durante la carga.

Lecturas de registros o slots ausentes y división por cero producen `ERROR`. La política documentada es que una instrucción fallida no incrementa `k`, porque no completó una transición. Los errores de sintaxis, versión, operandos o etiquetas son `ERROR_DE_CARGA`, ocurren antes de construir una ejecución y por ello tienen `k=0` y ninguna salida.

Los fallos físicos de memoria o escritura se clasifican como recursos del proceso; no se reinterpretan como propiedades gramaticales del programa.

## Alcance matemático

Estas reglas y los ejemplos automatizados permiten comprobar transiciones concretas de `LCERO_IR 1`. No constituyen, ni pretenden sustituir, una demostración de completitud de Turing.
