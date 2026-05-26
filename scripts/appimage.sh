#!/usr/bin/env bash
#
# appimage.sh — assemble + package mdv as an AppImage, PATCHELF-FREE.
#
# Why patchelf-free: the usual tools (linuxdeploy) stamp every bundled library
# with a `$ORIGIN` RUNPATH via patchelf. But patchelf — every version tested,
# 0.15 through 0.18 — corrupts libraries that carry RELR relocations
# (`.relr.dyn`), which Fedora's toolchain emits by default. A corrupted lib
# segfaults in its constructor (`_dl_init`) before main() runs, and *which* lib
# dies is random per build. So we never rewrite a single ELF: copy the dependency
# closure byte-pristine and resolve it at runtime via a wrapper AppRun that sets
# LD_LIBRARY_PATH. (See git history / DEVELOPMENT.md for the full diagnosis.)
#
# Run from the repo root after `make release`. Needs: appimagetool on PATH, the
# two excludelists in scripts/, and qmake6 (for the Qt plugin dir).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

VERSION="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9.]+).*/\1/p' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "appimage.sh: could not parse VERSION from CMakeLists.txt (need a 'VERSION x.y.z' line)" >&2; exit 1; }
ARCH="$(uname -m)"
RELEASE_DIR="${RELEASE_DIR:-build-release}"
DIST_DIR="${DIST_DIR:-dist}"
APPDIR="$RELEASE_DIR/AppDir"
QT_PLUGINS="$(qmake6 -query QT_INSTALL_PLUGINS)"

echo "==> AppImage: mdv $VERSION ($ARCH) — patchelf-free"
rm -rf "$APPDIR"; mkdir -p "$APPDIR/usr/lib" "$APPDIR/usr/plugins" "$DIST_DIR"

# 1. Install mdv via the same install() rules as `make install` (binary,
#    .desktop, AppStream metainfo, icon).
cmake --install "$RELEASE_DIR" --prefix "$APPDIR/usr" >/dev/null

# 2. Bundle the Qt plugins a GUI viewer needs (platform, image, icons, style).
for cat in platforms imageformats iconengines styles platformthemes generic; do
  [ -d "$QT_PLUGINS/$cat" ] && cp -rL "$QT_PLUGINS/$cat" "$APPDIR/usr/plugins/"
done

# 3. Copy the shared-library closure PRISTINE (recursive ldd), minus the
#    host-provided excludelist (upstream AppImage list + scripts/excludelist.mdv).
EXCL="$(cat scripts/excludelist scripts/excludelist.mdv \
  | grep -vE '^[[:space:]]*#|^[[:space:]]*$' | sed 's/[[:space:]]*#.*//;s/[[:space:]]//g' | sort -u)"
collect() {
  ldd "$1" 2>/dev/null | awk '/=> \// {print $3}' | while read -r so; do
    b="$(basename "$so")"
    printf '%s\n' "$EXCL" | grep -qxF "$b" && continue
    [ -e "$APPDIR/usr/lib/$b" ] && continue
    cp -L "$so" "$APPDIR/usr/lib/$b" && echo "$APPDIR/usr/lib/$b"
  done
}
queue="$APPDIR/usr/bin/mdv $(find "$APPDIR/usr/plugins" -name '*.so')"
while [ -n "$queue" ]; do
  next=""
  for f in $queue; do next="$next $(collect "$f")"; done
  queue="$next"
done
echo "    bundled $(ls "$APPDIR/usr/lib" | wc -l) libs (pristine)"

# 4. Wrapper AppRun: resolves the pristine libs via LD_LIBRARY_PATH — no per-lib
#    RUNPATH, so no ELF was rewritten and nothing is corrupted.
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
exec "$HERE/usr/bin/mdv" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# 5. Top-level icon, .DirIcon, and .desktop — appimagetool wants them at the root.
cp "$APPDIR/usr/share/icons/hicolor/512x512/apps/dev.gesslar.mdv.png" "$APPDIR/dev.gesslar.mdv.png"
ln -sf dev.gesslar.mdv.png "$APPDIR/.DirIcon"
cp "$APPDIR/usr/share/applications/dev.gesslar.mdv.desktop" "$APPDIR/"

# 6. Validate the AppDir against AppImage's own lint (advisory).
[ -x scripts/appdir-lint.sh ] && scripts/appdir-lint.sh "$APPDIR" || true

# 7. Package.
OUT="$DIST_DIR/mdv-$VERSION-$ARCH.AppImage"
rm -f "$OUT"
ARCH="$ARCH" appimagetool "$APPDIR" "$OUT"
echo "==> $OUT"
