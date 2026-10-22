# Módulo 0 — Infraestructura del compilador L-Cero

> Documento histórico del Módulo 0. El lexer incorporado posteriormente se
> documenta en `modulo_1_lexer_afd.md`.

## Objetivo

Este módulo establece la base técnica del compilador en C++17. Permite ubicar
el proyecto, procesar rutas, leer un fuente `.lc`, producir diagnósticos y
administrar de manera segura el futuro artefacto `.lci`.

No contiene lexer, parser, AST funcional, análisis semántico, optimización,
asignación de registros ni generación de instrucciones. Una ejecución válida
lee el archivo y termina expresamente con estado **compilador no implementado**;
no simula una compilación exitosa.

## Organización de carpetas

```text
LCero-Compilador/
├── Source/                    # Entrada predeterminada programa.lc
├── LCIs/                      # Destino de los futuros archivos .lci
├── include/lcero/             # Interfaces públicas del compilador
│   └── common/                # Diagnósticos, rutas, lectura y artefactos
├── src/
│   ├── main.cpp               # Adaptador de consola
│   ├── compiler.*             # Coordinador del pipeline
│   ├── common/                # Servicios compartidos
│   ├── lexer/                 # Reservado para Hito I.1
│   ├── parser/                # Reservado para Hito I.2
│   ├── ast/                   # Reservado para el AST real
│   ├── semantic/              # Reservado para Hito I.3
│   ├── ir/                    # Reservado para la RI interna y LCERO_IR 1
│   ├── optimizer/             # Reservado para optimización y registros
│   └── codegen/               # Reservado para la emisión final de .lci
├── tests/                     # Pruebas automatizadas sin framework externo
└── docs/                      # Documentación técnica y académica
```

Las carpetas de las fases futuras solo contienen `.gitkeep`. No se definieron
interfaces ficticias porque su forma debe surgir de las construcciones
matemáticas y de las decisiones de cada hito.

## Responsabilidades de los componentes

- `CommandLineParser`: valida argumentos y resuelve rutas explícitas o
  predeterminadas.
- `PathResolver`: encuentra la raíz buscando
  `LCero-Compilador.vcxproj` desde el directorio actual y, como alternativa,
  desde la ubicación del ejecutable. Esto evita depender de `Debug`, `Release`
  o `bin/<plataforma>/<configuración>`.
- `SourceReader`: verifica que la entrada exista, sea un archivo regular y
  pueda leerse completamente en modo binario. El contenido queda disponible
  para el futuro lexer sin transformar saltos de línea.
- `DiagnosticBag`: representación independiente de errores, advertencias e
  información. Admite categoría, gravedad, mensaje, archivo, línea y columna.
- `DiagnosticRenderer`: única responsabilidad de convertir diagnósticos a
  texto de consola. Las pruebas pueden inspeccionar `DiagnosticBag` sin analizar
  cadenas impresas.
- `OutputArtifact`: invalida una salida anterior y ofrece publicación
  transaccional. Escribe primero `<salida>.tmp` en el mismo directorio y solo
  después lo renombra al nombre definitivo. Un fallo limpia el temporal.
- `Compiler`: coordinador del futuro pipeline. Por ahora invalida la salida
  anterior, lee el fuente y devuelve `NotImplemented`.
- `main.cpp`: conecta consola, opciones, coordinador y renderizado; no contiene
  lógica de análisis del lenguaje.

## Punto de entrada y flujo actual

1. Obtiene el directorio de trabajo y la ubicación del ejecutable.
2. Localiza la raíz del proyecto.
3. Procesa los argumentos.
4. Informa las rutas de entrada y salida.
5. Invalida un `.lci` anterior destinado a esa compilación. De este modo, una
   ejecución fallida no deja un resultado viejo que parezca actual.
6. Lee por completo el archivo `.lc`.
7. Informa que la infraestructura funciona, pero el compilador todavía no está
   implementado.
8. Finaliza con código `2` y sin generar `.lci`.

Los códigos actuales son: `0` para ayuda o, en el futuro, compilación exitosa;
`1` para argumentos, rutas o entrada/salida inválidos; y `2` para la detención
intencional del Módulo 0.

## Argumentos y rutas

Sin argumentos:

```powershell
LCero.Compiler.exe
```

- entrada: `<raíz>/Source/programa.lc`
- salida reservada: `<raíz>/LCIs/programa.lci`

Con rutas explícitas:

```powershell
LCero.Compiler.exe archivo.lc -o archivo.lci
```

Las rutas explícitas relativas se interpretan desde el directorio de trabajo
del proceso. Si se indica una entrada sin `-o`, la salida se deriva como
`<raíz>/LCIs/<nombre-de-entrada>.lci`. Las extensiones `.lc` y `.lci` se validan
para reducir sobrescrituras accidentales.

## Diagnósticos

Las categorías preparadas son:

- entrada/salida;
- léxico;
- sintáctico;
- semántico;
- interno;
- generación de código;
- argumentos de línea de comandos.

La ubicación es opcional porque algunos errores afectan al proceso completo.
Cuando las fases formales estén implementadas, línea y columna comenzarán en 1,
según el contrato del lenguaje. Este módulo no crea detectores léxicos,
sintácticos ni semánticos.

## Preparación segura de `.lci`

`OutputArtifact` separa dos operaciones:

1. `invalidate_previous`: elimina un temporal abandonado y la salida final
   anterior antes de iniciar un nuevo intento.
2. `publish`: crea directorios, escribe y verifica el temporal, y lo renombra
   al nombre final únicamente al terminar.

El coordinador del Módulo 0 solo usa la primera operación. `publish` queda para
la fase que implemente el generador real conforme a `LCERO_IR 1`. La prueba de
publicación usa contenido controlado únicamente para verificar la mecánica de
archivos; el compilador no lo emite.

## Compilar y ejecutar desde Visual Studio

1. Abrir `LCero-Compilador.slnx`.
2. Elegir `Debug` o `Release` y `x64` o `x86`.
3. Establecer `LCero-Compilador` como proyecto de inicio.
4. Compilar la solución.
5. Ejecutar con F5 o Ctrl+F5.

El proyecto configura `$(ProjectDir)` como directorio de trabajo para todas las
configuraciones. Aunque se cambie, el buscador puede recuperar la raíz desde la
ubicación del ejecutable mientras se conserve la estructura configurada.

Para las pruebas, establecer `LCero-Compilador.Tests` como proyecto de inicio o
usar **Compilar solución** y ejecutar su binario desde una terminal.

## Compilar y ejecutar desde una terminal

Desde una *Developer PowerShell for Visual Studio* ubicada en la raíz:

```powershell
msbuild .\LCero-Compilador.vcxproj /m /p:Configuration=Debug /p:Platform=x64
msbuild .\tests\LCero-Compilador.Tests.vcxproj /m /p:Configuration=Debug /p:Platform=x64
.\bin\x64\Debug\LCero.Compiler.exe
.\bin\x64\Debug\LCero.Compiler.Tests.exe
```

También puede pasarse una entrada explícita:

```powershell
.\bin\x64\Debug\LCero.Compiler.exe .\Source\programa.lc -o .\LCIs\programa.lci
```

La primera ejecución termina con código `2` mientras el pipeline no exista.

## Pruebas del módulo

El ejecutable de pruebas comprueba:

- lectura exacta de un `.lc` existente;
- archivo inexistente;
- ruta no legible como archivo regular;
- rutas predeterminadas y explícitas;
- derivación de salida para una entrada explícita;
- argumentos inválidos;
- resolución de raíz desde el directorio y desde el ejecutable;
- invalidación de un resultado anterior y ausencia de `.lci` accidental;
- publicación temporal y final del servicio de artefactos.

Las pruebas crean espacios aislados bajo el directorio temporal del sistema y
los eliminan al finalizar. No requieren interacción ni dependencias externas.

## Pendiente para los próximos módulos

- definir tokens y construir manualmente AFN, AFD, minimización y lexer;
- transformar la EBNF en BNF, cerrar FIRST/FOLLOW y construir el parser LL(1);
- definir el AST real a partir de la gramática verificada;
- implementar tabla de símbolos e inicialización definida;
- traducir AST a RI interna;
- analizar vivacidad, optimizar, colorear registros y realizar spilling;
- emitir `LCERO_IR 1` usando `OutputArtifact::publish` solo tras el éxito total;
- ampliar las pruebas con casos léxicos, sintácticos, semánticos e integración.

El contrato `LCERO_IR 1` no fue modificado ni reinterpretado en este módulo.
