# mdv build shortcuts — a thin wrapper around CMake so you don't have to
# remember the flags. Everything here you could also type out with cmake.
#
# We use the Ninja generator: it parallelizes automatically (no -j needed) and
# does fast incremental rebuilds. Ninja is "single-config", so the build type
# (Debug vs Release) is fixed when you *configure* — hence Debug and Release
# get their own directories and coexist.
#
# A build dir can't switch generators in place; if you ever see a CMake
# "generator mismatch" error, run `make distclean` once and rebuild.
#
# Off the system-package path (Windows/macOS), the Qt installer doesn't carry
# KSyntaxHighlighting — install it via KDE Craft / MacPorts and tell the build
# where it lives by passing the prefix:
#
#     make PREFIX_PATH=C:/CraftRoot build      # Windows (Craft)
#     make PREFIX_PATH=/opt/local build        # macOS (MacPorts)
#
# See DEVELOPMENT.md for the per-platform install steps.

DEV_DIR     := build
RELEASE_DIR := build-release
DIST_DIR    := dist
PREFIX      ?= $(HOME)/.local

# Optional dependency prefix (Craft/MacPorts root). When set, it's handed to
# CMake as CMAKE_PREFIX_PATH so find_package() can locate KSyntaxHighlighting.
# Empty on Linux, where the system package manager already put it on the path.
PREFIX_PATH ?=
PREFIX_FLAG := $(if $(PREFIX_PATH),-DCMAKE_PREFIX_PATH=$(PREFIX_PATH),)

# Windows produces mdv.exe; everywhere else it's just mdv. GNU Make sets OS to
# Windows_NT on Windows, which is how we tell the `run` target where to look.
# The same host detection picks which packaging include to pull in (bottom of
# this file) — one file per OS so the dist logic stays out of the way here.
ifeq ($(OS),Windows_NT)
EXE := .exe
DIST_INCLUDE := Makefile.dist.windows
else ifeq ($(shell uname -s),Darwin)
EXE :=
DIST_INCLUDE := Makefile.dist.macos
else
EXE :=
DIST_INCLUDE := Makefile.dist.linux
endif

.PHONY: all build release run install uninstall clean distclean \
        format format-check tidy clazy lint

# `make` on its own gives you a dev build.
all: build

# Dev build (build/): no optimization, debug symbols, assertions on.
# CMAKE_EXPORT_COMPILE_COMMANDS writes build/compile_commands.json so
# clangd / Qt Creator can resolve Qt + KSyntaxHighlighting includes.
build:
	cmake -S . -B $(DEV_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug $(PREFIX_FLAG) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build $(DEV_DIR)

# Release build (build-release/): optimized, assertions off. What you ship.
release:
	cmake -S . -B $(RELEASE_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release $(PREFIX_FLAG)
	cmake --build $(RELEASE_DIR)

# Build the dev binary if needed, then launch it.
run: build
	$(DEV_DIR)/mdv$(EXE)

# Install the release build to PREFIX (default ~/.local) via the install()
# rules in CMakeLists.txt — the binary, the .desktop file, and the icon.
#
# Two dev-only post-install fixups (packages handle both themselves, so the
# shipped .desktop and CPack are left untouched):
#   1. ~/.local/bin usually isn't on the GUI session's PATH, so a menu click on
#      a bare `Exec=mdv` finds nothing. Rewrite Exec to the absolute path for
#      this prefix. (A package installs to /usr/bin, already on PATH, so its
#      Exec=mdv is fine.)
#   2. Refresh the desktop/menu caches so the entry appears and launches without
#      a relog. Guarded with `-`/command -v so they no-op off-KDE or where the
#      tools are missing (and on macOS, where no .desktop is installed at all).
install: release
	cmake --install $(RELEASE_DIR) --prefix $(PREFIX)
	-test -f "$(PREFIX)/share/applications/mdv.desktop" && sed -i 's|^Exec=mdv |Exec=$(PREFIX)/bin/mdv |' "$(PREFIX)/share/applications/mdv.desktop"
	-command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$(PREFIX)/share/applications"
	-command -v kbuildsycoca6 >/dev/null 2>&1 && kbuildsycoca6

# Reverse the install by replaying the manifest CMake wrote during `make
# install`. No PREFIX needed here — the manifest holds absolute paths.
# Refresh the caches afterwards so the removed entry drops out of the menu.
uninstall:
	cmake -DMANIFEST=$(RELEASE_DIR)/install_manifest.txt -P cmake/cmake_uninstall.cmake
	-command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$(PREFIX)/share/applications"
	-command -v kbuildsycoca6 >/dev/null 2>&1 && kbuildsycoca6

# Remove compiled objects but keep the configured dev directory.
clean:
	cmake --build $(DEV_DIR) --target clean

# Remove every build directory and the packaging output.
distclean:
	cmake -E rm -rf $(DEV_DIR) $(RELEASE_DIR) $(DIST_DIR)

# Linting & formatting. Three layers, all over our own src/ only (Qt and
# KSyntaxHighlighting headers are never touched):
#   format        rewrite sources to .clang-format  (the formatter)
#   format-check  report drift without writing       (CI-friendly)
#   tidy          clang-tidy: general C++ analysis    (reads .clang-tidy)
#   clazy         clazy: Qt-specific analysis
#   lint          format-check + tidy + clazy         (run the lot)
#
# tidy/clazy need build/compile_commands.json, which `make build` writes — so
# both depend on `build`. Ninja's incremental rebuild makes that near-free.
LINT_SRCS := $(wildcard src/*.cpp src/*.h)
LINT_CPP  := $(wildcard src/*.cpp)

format:
	clang-format -i $(LINT_SRCS)

format-check:
	clang-format --dry-run --Werror $(LINT_SRCS)

tidy: build
	clang-tidy -p $(DEV_DIR) $(LINT_CPP)

# clazy ships clazy-standalone, a LibTooling driver that reads the same compile
# DB. level1 is the sane default tier (level0 = safest, level2 = noisier).
# No-ops with a hint if clazy isn't installed.
clazy: build
	@command -v clazy-standalone >/dev/null 2>&1 \
	  && clazy-standalone -p $(DEV_DIR) -checks=level1 $(LINT_CPP) \
	  || echo "clazy-standalone not found — install clazy for Qt-specific checks"

lint: format-check tidy clazy

# Packaging targets (deb/rpm/dist, etc.) live in a per-OS include. The leading
# dash means a missing file is silently skipped, so the not-yet-written
# Makefile.dist.windows / .macos don't break `make` on those platforms.
-include $(DIST_INCLUDE)
