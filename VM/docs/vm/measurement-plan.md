# Propuesta de mediciones para el Hito III

## Estado

Esta entrega no implementa un profiler ni publica resultados de rendimiento. Lo siguiente es un plan reproducible para datos sintéticos finitos; no debe presentarse como mediciones ya realizadas.

## Magnitudes propuestas

| Magnitud | Inicio/fin sugeridos | Unidad | Riesgo a controlar |
|---|---|---|---|
| Tiempo de carga | antes de abrir `.lci` / después de formar `IrSource` | μs o ms | caché de disco y antivirus |
| Tiempo de validación | entrada a `validate_ir` / `ValidatedIr` o diagnóstico | μs o ms | tamaño de líneas y etiquetas |
| Tiempo de ejecución | antes de `interpret` / estado terminal | μs o ms | número de pasos y tamaño de enteros |
| Memoria física | working set antes/después de una fase | bytes | asignador, sistema operativo y Debug/Release |
| Tamaño de entrada | archivo `.lci` | bytes/líneas/instrucciones | directivas no ejecutables |
| Magnitud de `cpp_int` | valor absoluto de operandos/resultados | bits | cero y valores negativos |

Para un entero no nulo `x`, la longitud matemática puede registrarse como `msb(abs(x)) + 1`; para cero se define 0 o 1 según la convención del informe, que debe declararse. Esta medida describe representación de datos, no tiempo algorítmico por sí sola.

## Casos sintéticos finitos

1. Secuencias rectas de 10², 10³ y 10⁴ operaciones con `HALT`.
2. Archivos con cantidades crecientes de etiquetas y saltos siempre resolubles.
3. Memoria dispersa con pocos slots escritos y cotas `MEM` crecientes.
4. Sumas y multiplicaciones con operandos de 64, 256, 1024 y 4096 bits.
5. El mismo conjunto en Debug y Release, sin traza y con traza por separado.

Todo caso debe ser finito, tener presupuesto superior al número esperado de pasos y terminar en `HALT`. No se debe usar `--no-step-limit` para una entrada cuya terminación no haya sido establecida por construcción.

## Protocolo sugerido

- registrar CPU, RAM, versión de Windows, MSVC, CMake, Boost y configuración;
- realizar calentamiento separado y varias repeticiones;
- conservar mediana, dispersión y cantidad de muestras, no solo el mejor tiempo;
- separar carga, validación, ejecución y serialización de salida;
- evitar `PRINT` masivo si se pretende medir únicamente transiciones;
- repetir con traza desactivada y activada, informando el costo del observador;
- conservar generadores, semillas y hashes de cada `.lci` sintético.

En Windows, el tiempo puede tomarse con `std::chrono::steady_clock`; el working set puede observarse con APIs del sistema o herramientas externas documentadas. Incorporar cualquiera de esas mediciones al ejecutable sería una etapa distinta y no debe contaminar `stdout`.

## Interpretación correcta

Una curva empírica describe esta implementación, equipo y conjunto de datos. No demuestra por sí sola una cota asintótica `O`, `Ω` o `Θ`; esas afirmaciones requieren un análisis matemático del algoritmo y su modelo de costo.

Del mismo modo, alcanzar `LIMIT` o un timeout solo prueba que se agotó una cota concreta. No decide el problema de la parada ni demuestra que el programa sea infinito.
