#!/usr/bin/env bash
#
# install-dev-desktop.bash — register mdv's icon + launcher for the current
# user so the Wayland/X11 dock shows the app icon when running from Qt Creator
# (i.e. before `cmake --install`).
#
# On Wayland the dock icon is resolved by matching the window's app_id ("mdv",
# set via QGuiApplication::setDesktopFileName) to a .desktop file of the same
# basename — QApplication::setWindowIcon() alone is ignored there. This drops
# that match into ~/.local/share so uninstalled dev builds light up too.
#
# Usage: ./install-dev-desktop.bash
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ICON_SRC="$REPO/resources/icons/mdv.png"

DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
ICON_DIR="$DATA/icons/hicolor/512x512/apps"
APP_DIR="$DATA/applications"
DESKTOP="$APP_DIR/mdv.desktop"

mkdir -p "$ICON_DIR" "$APP_DIR"
install -m644 "$ICON_SRC" "$ICON_DIR/mdv.png"

# Point Exec at the first build binary we can find so the launcher also works
# if double-clicked; the dock-icon match only needs the basename + Icon line.
EXEC="mdv"
for cand in "$REPO/build/Desktop_Debug/mdv" "$REPO/build/mdv"; do
    if [[ -x "$cand" ]]; then
        EXEC="$cand"
        break
    fi
done

cat > "$DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Name=mdv
GenericName=Markdown Viewer
Comment=An offline Markdown viewer.
Exec="$EXEC" %F
Icon=mdv
Terminal=false
Categories=Office;Utility;
MimeType=text/markdown;
StartupWMClass=mdv
StartupNotify=true
EOF

# Best-effort cache refresh — harmless if the tools are absent.
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APP_DIR" || true
command -v gtk-update-icon-cache    >/dev/null 2>&1 && gtk-update-icon-cache -f -t "$DATA/icons/hicolor" >/dev/null 2>&1 || true

echo "Installed:"
echo "  $ICON_DIR/mdv.png"
echo "  $DESKTOP  (Exec=$EXEC)"
echo
echo "Restart mdv. If the dock icon doesn't refresh, log out/in (the compositor"
echo "caches app_id -> .desktop matches)."
