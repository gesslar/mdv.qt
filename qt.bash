#!/usr/bin/env bash
#
# qt.bash — install Qt Creator (and friends) for working on mdv.qt.
#
# Usage: ./qt.bash
#
set -euo pipefail

PACKAGES=(
    qt-creator         # the IDE
    qt6-doc            # offline Qt 6 docs (browsable inside Creator)
    ninja-build        # faster generator than make for CMake
    gdb                # debugger Qt Creator drives
    clang-tools-extra  # clangd, for code model + completion
)
# md4c is fetched and built in-tree by CMake (see CMakeLists.txt), so no
# md4c-devel package is required for development.

if ! command -v dnf >/dev/null 2>&1; then
    echo "error: this script expects dnf (Fedora). Adapt for your distro." >&2
    exit 1
fi

missing=()
for pkg in "${PACKAGES[@]}"; do
    if ! rpm -q "$pkg" >/dev/null 2>&1; then
        missing+=("$pkg")
    fi
done

if [[ ${#missing[@]} -eq 0 ]]; then
    echo "All packages already installed: ${PACKAGES[*]}"
    exit 0
fi

echo "Installing: ${missing[*]}"
sudo dnf install -y "${missing[@]}"

echo
echo "Done. Launch Qt Creator with:  qtcreator &"
echo "Then: File → Open File or Project → select $(pwd)/CMakeLists.txt"
