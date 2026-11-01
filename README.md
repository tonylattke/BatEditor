# 🦇 BatCode

Editor de texto en **C++17 + Qt 6** con **Git integrado (libgit2)**, para **Windows y Linux**.
Junta en una sola ventana lo que normalmente se hace con un editor de código y un cliente Git:
editar con cursores múltiples y, en el modo Git, revisar el historial y los diffs, preparar archivos y hacer commits.

> Todo el código es original. Las ideas de interfaz se tomaron del uso habitual de editores
> modernos, sin código ni recursos de ninguna otra aplicación.

BatCode tiene **dos modos**. Se cambia de uno a otro con el interruptor **[ ✎ Editor | ⎇ Git ]**
de la esquina superior derecha, o con `Ctrl+Shift+E` y `Ctrl+Shift+G`.

| Modo editor | Modo Git |
|---|---|
| ![Modo editor](docs/captura.png) | ![Modo Git](docs/captura-git.png) |

## Modo editor

Tiene dos partes: el **explorador de carpetas** a la izquierda (la carpeta abierta aparece como
nodo raíz bajo «CARPETAS»; se oculta y se muestra con `Ctrl+B`) y las **pestañas** con los
archivos abiertos.

- Pestañas que se pueden mover y cerrar, con el indicador ● de cambios sin guardar. Doble clic en
  la barra vacía crea un archivo nuevo.
- Resaltado de sintaxis: C/C++, C#, Python, JavaScript/TypeScript, Rust, Go, JSON, CMake,
  Shell, YAML/TOML/INI, SQL, HTML/XML y Markdown.
- **Cursores múltiples**:
  - `Ctrl+Clic`: añade o quita un cursor
  - `Ctrl+D`: selecciona la palabra o añade la siguiente coincidencia
  - `Alt+F3`: selecciona todas las coincidencias
  - `Ctrl+Shift+L`: divide la selección en líneas
  - `Ctrl+Alt+↑/↓`: añade un cursor arriba o abajo
  - `Esc`: vuelve a un solo cursor
  - Copiar y pegar por cursor: si pegas N líneas con N cursores, cada uno recibe la suya.
- Autoindentación, `Tab`/`Shift+Tab` por bloques y retroceso inteligente dentro de la sangría.
- `Ctrl+/` comenta o descomenta, `Ctrl+Shift+D` duplica líneas.
- Buscar y reemplazar (`Ctrl+F` / `Ctrl+H`) con texto o regex, mayúsculas y palabra completa.
  En el reemplazo puedes usar grupos (`\1`). Con `Alt+Enter` pones un cursor en cada coincidencia.
- `Ctrl+P`: ir a archivo con búsqueda difusa. `Ctrl+G`: ir a línea.
- Barra de estado con línea y columna, número de cursores, sangría y lenguaje.
- Respeta los finales de línea CRLF/LF de cada archivo y guarda de forma atómica.

**Fuente:** se usan las mismas fuentes que Sublime Text trae por defecto en cada sistema:
**Consolas** en Windows, **Menlo** en macOS y **DejaVu Sans Mono** en Linux, a 10 pt
(`Ctrl+=` / `Ctrl+-` para cambiar el tamaño). En Windows, la interfaz usa Segoe UI 9 pt.

## Modo Git

Cada repositorio abierto es una **pestaña** (botón **+** para abrir otro). Al abrir una carpeta en
el modo editor, si es un repositorio, también se añade aquí. Cada repositorio tiene una barra
superior con la rama actual, «Abrir en el editor» y «Actualizar» (`F5`), y **tres secciones**:

1. **Ubicaciones**: ramas, remotos, etiquetas, stashes, submódulos y worktrees, con su número.
   - Clic en una rama, rama remota, etiqueta o stash: salta a su commit.
   - Doble clic en una rama local (o menú contextual): cambia a esa rama, con confirmación.
     El cambio es «seguro»: no sobrescribe cambios locales que entren en conflicto.
   - Se oculta y se muestra con el botón ◧ de la barra.
2. **Commits / Archivos**
   - *Commits*: historial de todas las ramas (hasta 1000 commits) con línea de nodos, autor,
     fecha y etiquetas de ramas, remotos y tags. El primer elemento son los **cambios actuales**
     («2 sin preparar, 1 sin seguimiento · Confirmar cambios»).
   - *Archivos*: los archivos del commit seleccionado o de los cambios actuales. Al hacer clic
     en uno, el detalle se desplaza a su diff.
3. **Detalle**
   - Para un commit: título, descripción, autor, fecha, SHA completo y padres (con enlace al
     padre), y el diff de cada archivo con `+añadidas −borradas`. Cada archivo se puede plegar.
   - Para los cambios actuales: mensaje del commit, tu identidad de Git y el botón **Commit**
     (o `Ctrl+Enter`), más las secciones **Preparados**, **Sin preparar** y **Sin seguimiento**.
     Cada archivo tiene sus acciones (Preparar, Quitar, Descartar, Abrir) y cada sección tiene
     «Preparar todo» o «Quitar todo».

La sesión recuerda el modo, la carpeta, los archivos y los repositorios abiertos.

## Compilar en Linux

```bash
# Ubuntu / Debian
sudo apt install build-essential cmake ninja-build qt6-base-dev libgit2-dev libgl1-mesa-dev

# Fedora
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel libgit2-devel

# Arch
sudo pacman -S base-devel cmake ninja qt6-base libgit2

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BatCode            # o: ./build/BatCode ~/mi-proyecto
```

## Compilar en Windows

Necesitas **Visual Studio 2022** (carga de trabajo «Desarrollo para el escritorio con C++»),
**Qt 6** (instalador oficial de qt.io, componente *MSVC 2022 64-bit*) y **vcpkg** para libgit2.

```powershell
# 1) vcpkg (una sola vez)
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# 2) Configurar y compilar (desde "x64 Native Tools Command Prompt for VS 2022")
cd F:\Repository\BatCode
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2019_64
cmake --build build

# 3) Copiar las DLL de Qt junto al ejecutable
C:\Qt\6.7.3\msvc2019_64\bin\windeployqt.exe build\BatCode.exe
build\BatCode.exe
```

vcpkg lee `vcpkg.json` e instala libgit2 automáticamente, y su DLL se copia sola junto al `.exe`.
Ajusta `CMAKE_PREFIX_PATH` a la versión y la carpeta de Qt que tengas instaladas.
Si prefieres no usar el instalador de Qt, puedes añadir `"qtbase"` a las dependencias de
`vcpkg.json` (la primera compilación tarda bastante).

También puedes abrir la carpeta directamente en Visual Studio o en Qt Creator, porque ambos
entienden `CMakeLists.txt`.

## Estructura

```
src/
  main.cpp                 Punto de entrada y argumentos de línea de comandos
  MainWindow.*             Ventana, menús, cambio de modo, explorador de carpetas, pestañas y sesión
  ModeSwitch.*             Interruptor [Editor | Git] de la esquina superior derecha
  EditorTab.*              Pestaña = archivo + barra de búsqueda; carga y guardado
  CodeEditor.*             Editor: números de línea, cursores múltiples, sangría, búsqueda
  FindBar.*                Barra de buscar y reemplazar
  QuickOpenDialog.*        Ctrl+P con búsqueda difusa
  SyntaxHighlighter.*      Analizador léxico de resaltado (comentarios y cadenas multilínea)
  LanguageDefinitions.*    Palabras clave y reglas de cada lenguaje
  DiffHighlighter.*        Colores para diffs unificados
  GitRepository.*          Envoltorio RAII sobre libgit2: estado, diffs por archivo, stage,
                           commit, historial, ramas, remotos, tags, stashes, checkout
  GitWorkspace.*           Modo Git: una pestaña por repositorio
  RepositoryView.*         Las tres secciones de un repositorio (ubicaciones, commits, detalle)
  FileDiffWidget.*         Bloque plegable con el diff de un archivo y sus acciones
  Theme.*                  Paleta oscura y estilos
```

## Próximos pasos sugeridos

- Preparar cambios por bloque o por línea (*hunks*) desde la vista de diff.
- Diff lado a lado, números de línea en el diff y vista de *blame*.
- Grafo real de ramas en la lista de commits.
- Crear, fusionar y borrar ramas; `fetch`, `pull` y `push` (libgit2 con HTTPS/SSH).
- Búsqueda en el historial.
- Paleta de comandos (`Ctrl+Shift+P`) y minimapa.
- Resaltado con tree-sitter, temas configurables y ajustes en JSON.
