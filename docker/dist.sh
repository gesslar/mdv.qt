#!/usr/bin/env bash
#
# dist.sh — build EVERY Linux artifact (deb, rpm, AppImage, Flatpak) inside the
# pinned Fedora container (docker/Dockerfile). One image, one command, all
# formats — so a release is reproducible and CI is trivial: just run this.
# Wrapped by `make linux`.
#
# Docker-flavoured on purpose: Docker is the most widely available engine, and
# Docker Desktop runs Linux containers on Windows + macOS — so a dev on ANY OS
# can produce the Linux artifacts. A Windows dev builds the native installer
# (`make windows`) AND all four Linux formats (`make linux`) from one box. CD
# uses whatever engine the runner provides.
#
# - Runs as your uid/gid (`--user`) so dist/ artifacts are YOURS, not root. We
#   also bind-mount /etc/passwd read-only: docker's `--user` (unlike podman's
#   keep-id) leaves no passwd entry for the uid, which flatpak-builder wants.
# - The cmake formats (deb/rpm/AppImage) build into a container-local dir
#   (RELEASE_DIR=/tmp/build), so your host build trees are never touched.
# - Flatpak needs the sandbox escape hatches (--privileged, /dev/fuse,
#   seccomp=unconfined, XDG_RUNTIME_DIR) + --disable-rofiles-fuse (in the flatpak
#   target) + a persistent runtime cache so the 1.5 GB org.kde.Platform pull
#   happens only on the first run.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
DOCKER="${DOCKER:-docker}"            # override to point at a specific docker CLI
IMAGE="mdv-build"
FLATPAK_CACHE="${FLATPAK_CACHE:-$HOME/.cache/mdv-flatpak}"

echo "==> build image: $IMAGE"
"$DOCKER" build -t "$IMAGE" docker/

mkdir -p "$FLATPAK_CACHE"
echo "==> build deb + rpm + AppImage + Flatpak in $IMAGE (as uid $(id -u))"
# --platform=linux/amd64: run as x86_64 regardless of host (no-op on x86_64,
# emulated on arm64) — matches the pinned-x86_64 image + toolchain above.
"$DOCKER" run --rm --platform=linux/amd64 \
  --user "$(id -u):$(id -g)" --security-opt label=disable \
  --privileged --device /dev/fuse --security-opt seccomp=unconfined \
  -v "$REPO":/work -w /work \
  -v /etc/passwd:/etc/passwd:ro \
  -e HOME=/tmp/fphome -e XDG_RUNTIME_DIR=/tmp/xdg \
  -v "$FLATPAK_CACHE":/tmp/fphome/.local/share/flatpak \
  "$IMAGE" bash -euc '
    mkdir -p /tmp/fphome /tmp/xdg && chmod 700 /tmp/xdg
    flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
    make deb rpm appimage RELEASE_DIR=/tmp/build
    make flatpak
  '
echo "==> artifacts in dist/:"
ls -1 dist/*.deb dist/*.rpm dist/*.AppImage dist/*.flatpak 2>/dev/null
