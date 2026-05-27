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
> On Windows neither is packaged, so CMake fetches and builds them in-tree
> (md4c, plus KSyntaxHighlighting + extra-cmake-modules) — nothing to install
> beyond Qt + a C++ toolchain. On macOS, KSyntaxHighlighting isn't carried by
> Homebrew; install it via MacPorts (or KDE Craft) and point CMake at it with
> `-DCMAKE_PREFIX_PATH`. The per-platform steps are below.

## Installing dependencies

### Fedora

```bash
sudo dnf install qt6-qtbase-devel qt6-qtbase-private-devel md4c-devel kf6-syntax-highlighting-devel cmake ninja-build gcc-c++
```

Add `qt-creator gdb clang-tools-extra` if you want the IDE and debugger.

### Debian / Ubuntu

```bash
sudo apt install qt6-base-dev qt6-base-private-dev libmd4c-dev libkf6syntaxhighlighting-dev cmake ninja-build g++
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

Install the official **[Qt online installer](https://www.qt.io/download-qt-installer)** and select Qt 6.5+ with **MinGW 64-bit** (the version this project was developed against; MSVC 2022 64-bit also works). Include **Qt Linguist / qttools** in the install — KSyntaxHighlighting's translation step needs `lrelease` on `PATH`. The Qt installer also bundles CMake, Ninja, and Qt Creator.

Two extra prereqs that the Qt installer doesn't provide:

- **Git for Windows** — `git` is needed at *configure* time (CMake clones extra-cmake-modules from invent.kde.org) and Git for Windows also ships a Perl at `C:\Program Files\Git\usr\bin\perl.exe`, which is what KSyntaxHighlighting needs to generate its data tables. CMakeLists auto-detects this Perl when one isn't already on `PATH`. If you'd rather use Strawberry Perl: `choco install strawberryperl`.
- **NSIS 3.x** (`makensis.exe`) — only needed for `make windows` (building the installer). `choco install nsis`, or download from <https://nsis.sourceforge.io/> and add `NSIS\Bin` to `PATH`.

Neither **md4c** nor **KSyntaxHighlighting** has a Windows package, so CMake fetches and builds both in-tree on Windows (along with extra-cmake-modules, which KSH needs). Nothing extra to install for them — `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release` and `cmake --build build-release` is enough on a clean machine that has Qt + a compiler + Git.

The first configure also clones and locally installs **extra-cmake-modules** into `build-release/_ecm-prefix/` (a build-tree-only install — nothing leaks into the system). That step is cached, so reconfigures don't re-clone.

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

## Packaging (Linux)

`.deb` and `.rpm` are built with CPack from the same `install()` rules `make
install` uses (binary + `.desktop` + icon). The targets live in
`Makefile.dist.linux` — the top-level Makefile pulls in the per-OS dist include
for the host automatically. Package metadata (dependencies, license, sections)
lives in `cmake/Packaging.cmake`.

```bash
make deb     # → dist/mdv_<version>_<arch>.deb
make rpm     # → dist/mdv-<version>-1.<arch>.rpm
make dist    # both
```

Each builds the release binary first, then runs `cpack`; artifacts land in
`dist/`.

**Packaging tooling** (not build deps — only needed to produce the packages):

- Fedora: `sudo dnf install rpm-build dpkg`
- Debian/Ubuntu: `sudo apt install dpkg-dev rpm`

The Debian `Depends:` list is hand-maintained in `cmake/Packaging.cmake` — we
don't run `dpkg-shlibdeps`, since on a non-Debian host it resolves this distro's
soname packages, which are the wrong names for a `.deb`. Keep it in step with
the link line if you add a library. RPM `Requires:` are auto-detected by
`rpmbuild`.

### Flatpak

A single-file `.flatpak` is built with `flatpak-builder` against the KDE runtime
(`org.kde.Platform` 6.9), which already ships Qt 6 and KSyntaxHighlighting — so
only md4c is built alongside mdv. The manifest `dev.gesslar.mdv.yml` builds
straight from this checkout (no pushed tag required).

```bash
make flatpak   # → dist/mdv-<version>-<arch>.flatpak
```

**Tooling** (packaging dep, not a build dep): `flatpak` + `flatpak-builder`, with
the flathub remote configured:

- Fedora: `sudo dnf install flatpak flatpak-builder`
- Debian/Ubuntu: `sudo apt install flatpak flatpak-builder`
- `flatpak remote-add --if-not-exists --user flathub https://flathub.org/repo/flathub.flatpakrepo`

The first `make flatpak` pulls `org.kde.Platform` / `org.kde.Sdk` 6.9 (a large
one-time download). Install and run the bundle with:

```bash
flatpak install --user dist/mdv-*.flatpak   # e.g. mdv-2.0.0-x86_64.flatpak
flatpak run dev.gesslar.mdv                 # run/install are by app-id, not filename
```

For a Flathub submission, repoint the `mdv` module source from this local
checkout to a pinned remote (url + tag + commit).

### AppImage

A single-file `.AppImage` is built with `appimagetool`, but the dependency
deploy is **hand-rolled** (`scripts/appimage.sh`) rather than via linuxdeploy.
The reason: patchelf — which linuxdeploy uses to stamp each bundled library with
an `$ORIGIN` RUNPATH — corrupts libraries carrying RELR relocations
(`.relr.dyn`, which Fedora's toolchain emits by default), so a random lib
segfaults in `_dl_init` before `main()`. Instead we copy the dependency closure
**byte-pristine** and resolve it at runtime with a wrapper `AppRun` that sets
`LD_LIBRARY_PATH` — no ELF is ever rewritten.

```bash
make appimage   # → dist/mdv-<version>-<arch>.AppImage
```

**Tooling** (packaging dep, not a build dep): `appimagetool` on `PATH`. Bundled
libraries exclude the host-provided set per `scripts/excludelist` (the upstream
AppImage list, kept byte-identical and diffable) plus `scripts/excludelist.mdv`
(our additions — the systemd/dbus/crypto/network libs Qt drags in that a local
viewer never needs). `scripts/appdir-lint.sh` (also straight from the AppImage
project) validates the AppDir against AppImageHub's rules.

### All formats in a container (`make linux`)

`make linux` builds **every** format above — `.deb`, `.rpm`, `.AppImage`, and
`.flatpak` — inside a pinned Fedora container (`docker/Dockerfile`), so a release
is reproducible and CI is trivial: one image, one command. The image is the
toolchain only; your checkout is mounted at run time and the build just calls the
same `make` targets above, so it can't drift from the documented build.

```bash
make linux   # → dist/ : the .deb, .rpm, .AppImage and .flatpak, all at once
```

**Tooling**: Docker — nothing else; the image carries the full toolchain. Docker
is the most widely available engine, and since Docker Desktop runs Linux
containers on Windows and macOS, a dev on *any* OS can produce the Linux
artifacts — a Windows dev builds the native installer (`make windows`) **and** all
four Linux formats (`make linux`) from one box. `docker/dist.sh` runs the
container as your uid/gid (`--user`, plus a read-only `/etc/passwd` mount so
flatpak-builder can resolve the user), so the artifacts are yours, not root.
flatpak-builder inside a container needs sandbox escape hatches (`--privileged`,
`/dev/fuse`, `seccomp=unconfined`, `XDG_RUNTIME_DIR`) plus `--disable-rofiles-fuse`;
the script handles all of it and caches the `org.kde.Platform` runtime under
`~/.cache/mdv-flatpak`, so the ~1.5 GB pull happens only on the first run.

> A Fedora base has the same recent glibc + RELR libs as the host, so the
> AppImage's glibc floor is unchanged — the win is reproducibility/CI, not
> old-distro portability. The AppImage stays patchelf-free in the container too.

macOS will get its own `Makefile.dist.macos` include when that build path is
ready.

## Packaging (Windows)

The Windows installer is an NSIS `.exe` built by a custom `makensis` target
(not CPack — see `cmake/Packaging.cmake`) from the same `install()` rules
`cmake --install` uses (the binary, the icon, and a
windeployqt step that pulls in Qt + plugins + translations + the mingw
runtime DLLs). The target lives in `Makefile.dist.windows` — the top-level
Makefile pulls in the per-OS dist include for the host automatically.
Installer metadata (file associations, shortcuts, icons) lives in
`cmake/Packaging.cmake`.

```powershell
make windows   # → dist\mdv-<version>-win64.exe
make dist      # same (alias, parallel to Linux's `make dist`)
```

`make windows` builds the release binary first, then drives the custom
`package_nsis` target (`cmake --install` + `makensis`); the artifact lands in
`dist\`. **Packaging tooling**: NSIS 3.x must be
installed and `makensis.exe` on `PATH` (see the Windows install section
above).

The resulting installer:

- Starts **unprivileged** (manifest `requestedExecutionLevel="asInvoker"`).
  No UAC prompt appears on launch.
- Shows a custom "Choose Installation Type" page after the licence with two
  radio buttons:
  - **Install for me only** (default) → installs to `%LOCALAPPDATA%\Programs\mdv\`,
    HKCU. No elevation needed.
  - **Install for all users** → installs to `Program Files\mdv\`, HKLM. If
    the user isn't already an admin, the installer **relaunches itself with
    `runas`** at this point, which is what finally triggers the UAC prompt.
    The elevated instance picks up a `/AllUsers` flag and skips the choice
    page so the user isn't asked twice.
- The next page lets the user override the install directory.
- Lays down `bin\mdv.exe` + Qt DLLs + plugins + translations + `mdv.ico`,
  matching the windeployqt layout (`qt.conf` in `bin\` points the runtime at
  `..\plugins` and `..\translations`).
- Creates Start Menu and Desktop shortcuts. NSIS resolves `$SMPROGRAMS` /
  `$DESKTOP` against the chosen install scope — per-user installs target
  the user's own shell folders (including OneDrive-synced Desktops);
  per-machine installs target the All Users folders.
- Registers file associations for `.md` / `.markdown` / `.mkd` against an
  `mdv.markdown` ProgID under `SHCTX\Software\Classes` (= `HKCU` per-user,
  `HKLM` per-machine). The uninstaller removes them.
- Writes an Add/Remove Programs entry under the same hive so the install
  shows up in Apps & Features with publisher, version, homepage, and a
  scope-aware uninstall command (`/CurrentUser` or `/AllUsers`).

Two NSIS-related notes for anyone editing `cmake/mdv.nsi.in`:

- **We don't use CPack's bundled NSIS template** — it hard-codes
  `RequestExecutionLevel admin`, which forces UAC at launch even on per-user
  installs. The custom `.nsi.in` is driven by a `package_nsis` CMake target.
  Linux's CPack path for `.deb` / `.rpm` is unaffected.
- **We don't use NSIS's `MultiUser.nsh` either** — its `Highest` execution
  level also triggers UAC at launch (before the choice page), and `Standard`
  never elevates at all. Neither matches "elevate only if the user picks
  per-machine". We hand-roll the elevation dance in ~50 lines using
  `nsDialogs` + `ExecShell "runas"`.

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
docs/
  THEMING.md     # how to author content themes
  templating.md  # the content-stylesheet templating internals
```

## Settings & state

Recent files persist via `QSettings`:

- **Linux**: `~/.config/gesslar/mdv.conf`
- **macOS**: `~/Library/Preferences/com.gesslar.mdv.plist`
- **Windows**: `HKEY_CURRENT_USER\Software\gesslar\mdv`

(Organisation/application names are set in `main.cpp`.)
