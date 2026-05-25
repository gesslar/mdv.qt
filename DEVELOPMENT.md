# Development

## Prerequisites

- **Qt 6.5 or newer** (Widgets module)
- **md4c** — CommonMark + GFM markdown parser
- **KSyntaxHighlighting** (KDE Frameworks 6) — code-block syntax highlighting
- **CMake 3.21 or newer**
- **Ninja** (the build generator used here)
- A C++20 compiler — GCC 11+, Clang 14+, MSVC 2022

> The rule for the two non-Qt deps: take them from the system package manager
> wherever it has them. On Linux that's both (install with the commands below).
> md4c has no package on Windows, so CMake fetches and builds it in-tree there —
> nothing to install. KSyntaxHighlighting isn't carried by the Qt installer on
> Windows or by Homebrew on macOS; install it via KDE Craft (or MacPorts) and
> point CMake at it with `-DCMAKE_PREFIX_PATH`. The per-platform steps are below.

## Installing dependencies

### Fedora

```bash
sudo dnf install qt6-qtbase-devel md4c-devel kf6-syntax-highlighting-devel cmake ninja-build gcc-c++
```

Add `qt-creator gdb clang-tools-extra` if you want the IDE and debugger.

### Debian / Ubuntu

```bash
sudo apt install qt6-base-dev libmd4c-dev libkf6syntaxhighlighting-dev cmake ninja-build g++
# Optional IDE:
sudo apt install qtcreator
```

### Arch

```bash
sudo pacman -S qt6-base md4c syntax-highlighting cmake ninja gcc qtcreator
```

### macOS

The official **[Qt online installer](https://www.qt.io/download-qt-installer)** is the easiest path. Pick Qt 6.5+ with the Widgets module. Then:

```bash
brew install md4c cmake ninja
```

Or via Homebrew end-to-end:

```bash
brew install qt md4c cmake ninja
```

(Homebrew's `qt` is sometimes a release behind upstream — fine for this project, but verify with `qmake6 --version`.)

KSyntaxHighlighting (KDE Frameworks 6) isn't in Homebrew core; on macOS install it via **MacPorts** (`port install kf6-syntax-highlighting`) or a KDE Craft build, and point CMake at it with `-DCMAKE_PREFIX_PATH=...`. macOS support is best-effort — primary development is on Linux.

### Windows

Install the official **[Qt online installer](https://www.qt.io/download-qt-installer)** and select Qt 6.5+ with **MSVC 2022 64-bit** (or MinGW if you prefer). The installer also bundles CMake, Ninja, and Qt Creator.

If you'd rather use system tooling: install **Visual Studio 2022** (with the "Desktop development with C++" workload) and a standalone CMake.

**md4c** has no Windows package, so CMake fetches and builds it in-tree — nothing to install.

**KSyntaxHighlighting** isn't bundled by the Qt installer. Get it from **[KDE Craft](https://community.kde.org/Craft)**, KDE's build system for Windows (it pulls in extra-cmake-modules and the syntax/theme data for you):

```powershell
# In a Craft shell, after the one-time Craft setup:
craft kf6-syntax-highlighting
```

Then point CMake at the Craft prefix when configuring:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/CraftRoot
```

(Use your actual `CraftRoot` path. The same `-DCMAKE_PREFIX_PATH` trick is how the macOS MacPorts/Craft install gets found.)

## Building

The repo ships a `Makefile` that wraps CMake (Ninja generator):

```bash
make            # dev build (Debug) → build/mdv
make run        # build, then launch
make release    # optimized build → build-release/mdv
make install    # install the release build to ~/.local
make clean      # remove build artifacts
make distclean  # remove the build directories
```

Ninja parallelizes automatically, and the dev build emits
`build/compile_commands.json` for clangd / Qt Creator. Debug and Release use
separate directories because the build type is fixed at configure time.

Run the binary directly:

```bash
./build/mdv                   # empty window
./build/mdv README.md         # opens a markdown file
./build/mdv ~/Documents/*.md  # multiple files, one tab each
```

To drive CMake without the Makefile:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Using Qt Creator

1. `File → Open File or Project`
2. Pick `CMakeLists.txt` at the root
3. Pick a kit (Qt 6.x + your compiler)
4. Build / Run from the toolbar

The repo includes a Qt Creator color scheme variant at `/projects/Blackboard.xml`
(in this development tree). To install it system-wide:

- Linux: `cp Blackboard.xml ~/.config/QtProject/qtcreator/styles/`
- macOS: `cp Blackboard.xml ~/Library/Preferences/QtProject/qtcreator/styles/`
- Windows: `copy Blackboard.xml %APPDATA%\QtProject\qtcreator\styles\`

Then in Qt Creator: **Edit → Preferences → Text Editor → Fonts & Colors → Blackboard**.

## Project layout

```
src/
  main.cpp                  # entry point, QApplication, CLI args
  MainWindow.{h,cpp}        # top-level window, menus, shortcuts
  EditorArea.{h,cpp}        # splitter tree, active-group tracking, file routing
  EditorGroup.{h,cpp}       # one tabbed editor group (QTabWidget subclass)
  TabBar.{h,cpp}            # QTabBar with cross-group drag support
  DocumentView.{h,cpp}      # one tab — owns a QTextBrowser and a file path
  Markdown.{h,cpp}          # md4c → HTML, with code-block post-processing
  Highlighter.{h,cpp}       # KSyntaxHighlighting → themed inline-styled spans
  ContentTheme.{h,cpp}      # loads a theme JSON, resolves the QSS template
  PreferencesDialog.{h,cpp} # theme + font settings
resources/
  content/content.qss.template  # structural CSS for rendered markdown
  themes/*.content.json         # bundled content themes
  icons/mdv.png                 # app icon
examples/              # showcase docs (markdown elements, syntax highlighting)
CMakeLists.txt
Makefile               # build shortcuts (wraps CMake + Ninja)
THEMING.md             # how to author content themes
```

## Settings & state

Recent files persist via `QSettings`:

- **Linux**: `~/.config/gesslar/mdv.conf`
- **macOS**: `~/Library/Preferences/com.gesslar.mdv.plist`
- **Windows**: `HKEY_CURRENT_USER\Software\gesslar\mdv`

(Organisation/application names are set in `main.cpp`.)
