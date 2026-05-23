# Development

## Prerequisites

- **Qt 6.5 or newer** (Widgets module)
- **CMake 3.21 or newer**
- A C++20 compiler — GCC 11+, Clang 14+, MSVC 2022

## Installing dependencies

### Fedora

A helper script bundles the packages I personally use, including Qt Creator:

```bash
./qt.bash
```

Or directly:

```bash
sudo dnf install qt-creator qt6-doc ninja-build gdb clang-tools-extra
```

Qt 6 itself is pulled in transitively by `qt-creator`; if you don't want the IDE, install the base packages:

```bash
sudo dnf install qt6-qtbase-devel cmake ninja-build gcc-c++
```

### Debian / Ubuntu

```bash
sudo apt install qt6-base-dev cmake ninja-build g++
# Optional IDE:
sudo apt install qtcreator
```

### Arch

```bash
sudo pacman -S qt6-base cmake ninja gcc qtcreator
```

### macOS

The official **[Qt online installer](https://www.qt.io/download-qt-installer)** is the easiest path. Pick Qt 6.5+ with the Widgets module. Then:

```bash
brew install cmake ninja
```

Or via Homebrew end-to-end:

```bash
brew install qt cmake ninja
```

(Homebrew's `qt` is sometimes a release behind upstream — fine for this project, but verify with `qmake6 --version`.)

### Windows

Install the official **[Qt online installer](https://www.qt.io/download-qt-installer)** and select Qt 6.5+ with **MSVC 2022 64-bit** (or MinGW if you prefer). The installer also bundles CMake, Ninja, and Qt Creator.

If you'd rather use system tooling: install **Visual Studio 2022** (with the "Desktop development with C++" workload) and a standalone CMake.

## First build

```bash
cmake -S . -B build
cmake --build build -j
```

The executable lands at `build/mdv`. Run it:

```bash
./build/mdv                   # empty window
./build/mdv README.md         # opens a markdown file
./build/mdv ~/Documents/*.md  # multiple files, one tab each
```

### Faster iterative builds with Ninja

```bash
cmake -S . -B build -G Ninja
cmake --build build           # subsequent builds are noticeably quicker
```

### Build type

The default is `Debug`. For an optimized binary:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
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
  main.cpp             # entry point, QApplication, CLI args
  MainWindow.{h,cpp}   # top-level window, menus, shortcuts
  EditorArea.{h,cpp}   # splitter tree, active-pane tracking, file routing
  EditorPane.{h,cpp}   # one tabbed editor group (QTabWidget subclass)
  TabBar.{h,cpp}       # QTabBar with cross-pane drag support
  DocumentView.{h,cpp} # one tab — owns a QTextBrowser and a file path
  Theme.{h,cpp}        # syntax-highlight palette (Blackboard)
CMakeLists.txt
qt.bash                # Fedora dependency installer
```

## Settings & state

Recent files persist via `QSettings`:

- **Linux**: `~/.config/gesslar/mdv.conf`
- **macOS**: `~/Library/Preferences/com.gesslar.mdv.plist`
- **Windows**: `HKEY_CURRENT_USER\Software\gesslar\mdv`

(Organisation/application names are set in `main.cpp`.)
