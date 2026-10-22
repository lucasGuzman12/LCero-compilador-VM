# Guías de uso de la máquina virtual L-Cero

Esta carpeta reúne el recorrido recomendado para una persona que acaba de descargar el repositorio. No hace falta conocer la implementación en C++ para ejecutar un programa, pero sí debe distinguirse el archivo fuente `.lc` del archivo intermedio `.lci`: esta VM consume exclusivamente `LCERO_IR 1`.

## Recorrido recomendado

1. [`01-instalacion-y-compilacion.md`](01-instalacion-y-compilacion.md): instalar las herramientas y generar el ejecutable.
2. [`02-primer-programa.md`](02-primer-programa.md): validar y ejecutar el primer `.lci`.
3. [`03-pruebas-incluidas.md`](03-pruebas-incluidas.md): ejecutar los 40 contratos y los 6 programas del compilador.
4. [`04-trazas-y-depuracion.md`](04-trazas-y-depuracion.md): observar `C=(P,pc,R,M,O,k)` sin cambiar la ejecución.
5. [`05-errores-y-soluciones.md`](05-errores-y-soluciones.md): interpretar estados, códigos de salida y fallos comunes.
6. [`06-integracion-con-el-compilador.md`](06-integracion-con-el-compilador.md): ejecutar otros programas producidos por el compilador.
7. [`07-portabilidad-y-privacidad.md`](07-portabilidad-y-privacidad.md): conocer cómo se resuelven las rutas y qué no debe publicarse.
8. [`08-cmake-y-vcpkg.md`](08-cmake-y-vcpkg.md): detectar CMake, configurar vcpkg y generar Debug/Release.
9. [`09-comandos-de-powershell.md`](09-comandos-de-powershell.md): referencia práctica de los comandos usados en la terminal.

## Regla de portabilidad

Ninguna guía supone un nombre de usuario o una ubicación fija. Cuando se necesita una ruta absoluta, PowerShell la calcula desde la carpeta en la que se descargó el repositorio:

```powershell
Set-Location "<carpeta donde descargó el repositorio>"
$repo = (Resolve-Path -LiteralPath ".").Path
$vm = Join-Path $repo 'build\bin\Debug\lcero-vm.exe'
```

El texto entre `<...>` es una indicación para el usuario, no una ruta guardada por el sistema. Las rutas con espacios y caracteres Unicode son válidas cuando se pasan como un único argumento.

## Qué componente hace cada tarea

```text
programa.lc --[compilador externo]--> programa.lci --[esta VM]--> salida
                                              |
                                              +--> estado y diagnóstico
```

La separación es parte del diseño de lenguajes formales: el compilador decide cómo traducir el lenguaje fuente y la VM valida y ejecuta solamente la representación intermedia acordada. La ejecución de ejemplos no constituye por sí sola una demostración de completitud de Turing.

