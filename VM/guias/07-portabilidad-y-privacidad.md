# 7. Portabilidad de rutas y privacidad

## Resultado de la auditoría

Se revisaron código C++, cabeceras, CMake, presets, scripts PowerShell, pruebas, fixtures y documentación. Se excluyeron únicamente `.git`, porque es metadato interno de Git, y `build`, porque contiene artefactos generados para la máquina que compiló.

No se encontraron en los archivos distribuibles:

- rutas de perfiles de Windows;
- nombres del usuario o de las carpetas locales usadas durante el desarrollo;
- rutas Windows absolutas escritas en el código o las guías;
- valores con patrones habituales de contraseñas, tokens o claves de GitHub.

Los antiguos ejemplos que fijaban la instalación de Visual Studio o usaban una ruta ilustrativa fueron reemplazados por `vswhere`, variables de entorno, `$PSScriptRoot`, `Resolve-Path` y `Join-Path`.

## Cómo se calculan las rutas

| Componente | Origen de la ruta |
|---|---|
| CMake | `${sourceDir}`, `${CMAKE_CURRENT_SOURCE_DIR}` y `${CMAKE_BINARY_DIR}` |
| Preset vcpkg | `$env{VCPKG_ROOT}` |
| Scripts | ubicación real del script mediante `$PSScriptRoot` |
| Runners | raíz resuelta mediante `Resolve-Path` y archivos unidos con `Join-Path` |
| VM | CWD primero; luego ancestros de la ubicación real del `.exe` obtenida por Windows |

Estas rutas se convierten en absolutas en tiempo de configuración o ejecución, dentro del equipo donde se descargó el sistema. No se reutiliza la ubicación del equipo que creó el repositorio.

La búsqueda de un `.lci` es una convención del proceso anfitrión y ocurre antes del loader. No introduce tokens, instrucciones o reglas nuevas en `LCERO_IR 1`.

## Carpetas que no se publican

`.gitignore` excluye, entre otros:

- `build`, `out`, `.vs`, `.vscode` y cachés de CMake/MSVC;
- ejecutables, bibliotecas, PDB y soluciones generadas;
- `AGENTS.md`, `.agents`, `.codex` y memorias locales;
- `.env`, `secrets`, `private`, credenciales y claves privadas;
- trazas, capturas y temporales.

Los archivos `test/contracts/expected/*.out` sí se conservan: no son binarios compilados, sino salidas contractuales que se comparan byte a byte.

## Comprobación antes de publicar

```powershell
git status --short --untracked-files=all
git check-ignore -v AGENTS.md .env secrets\credencial.json
git grep -n -I -E "([A-Za-z]:\\|password|passwd|api[_-]?key|access[_-]?token)" -- .
```

Cada coincidencia debe revisarse en contexto: una palabra académica como “tokens del lexer” no es una credencial. No suba `build`; configúrelo nuevamente en cada equipo con la [guía de instalación](01-instalacion-y-compilacion.md).

`gitignore` no puede ocultar el nombre y correo de autor ya asociados a los commits. Antes de publicar, el responsable puede revisarlos sin modificar el historial:

```powershell
git log --all --format='%h %an <%ae>'
```

Cambiar o reescribir autores altera el historial y debe ser una decisión explícita del equipo; esta preparación no lo hace automáticamente.

