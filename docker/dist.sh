#!/usr/bin/env bash
#
# dist.sh — build Linux artifact(s) (deb, rpm, AppImage, Flatpak) inside the
# pinned Fedora container (docker/Dockerfile). Wrapped by `make linux`.
#
# Usage:
#   dist.sh             # all four formats (default; what `make linux` runs)
#   dist.sh deb         # only the .deb
#   dist.sh rpm         # only the .rpm
#   dist.sh appimage    # only the .AppImage
#   dist.sh flatpak     # only the .flatpak
#
# The per-format invocation is what the CI workflow uses so it can matrix the
# four builds over their own runners in parallel rather than chain them in one
# container.
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
FORMAT="${1:-all}"

case "$FORMAT" in
  all|deb|rpm|appimage|flatpak) ;;
  *) echo "unknown format: $FORMAT (expected: all | deb | rpm | appimage | flatpak)" >&2; exit 2 ;;
esac

echo "==> build image: $IMAGE"
"$DOCKER" build -t "$IMAGE" docker/

mkdir -p "$FLATPAK_CACHE"
echo "==> build $FORMAT in $IMAGE (as uid $(id -u))"
# --platform=linux/amd64: run as x86_64 regardless of host (no-op on x86_64,
# emulated on arm64) — matches the pinned-x86_64 image + toolchain above.
"$DOCKER" run --rm --platform=linux/amd64 \
  --user "$(id -u):$(id -g)" --security-opt label=disable \
  --privileged --device /dev/fuse --security-opt seccomp=unconfined \
  -v "$REPO":/work -w /work \
  -v /etc/passwd:/etc/passwd:ro \
  -e HOME=/tmp/fphome -e XDG_RUNTIME_DIR=/tmp/xdg \
  -v "$FLATPAK_CACHE":/tmp/fphome/.local/share/flatpak \
  -e FORMAT="$FORMAT" \
  "$IMAGE" bash -euc '
    mkdir -p /tmp/fphome /tmp/xdg && chmod 700 /tmp/xdg

    # Wrapper for appstreamcli that strips XDG_RUNTIME_DIR from its environment.
    #
    # Why: appstreamcli compose uses glycin to load PNG icons for thumbnailing,
    # and glycin sandboxes its image loader via bwrap. When constructing the
    # bwrap command, glycin first sets `--setenv XDG_RUNTIME_DIR /tmp-run` to
    # match its own sandbox-internal tmpfs, then forwards the parent process`s
    # XDG_RUNTIME_DIR with a second `--setenv` — and the second wins. In the
    # container, the parent`s XDG_RUNTIME_DIR is /tmp/xdg (set above so
    # flatpak-builder can talk to its own dbus), but /tmp/xdg does NOT exist
    # inside glycin`s bwrap sandbox (only /tmp-home and /tmp-run are mounted
    # as tmpfs there). The loader process crashes at startup with exit code 1,
    # and appstreamcli surfaces that as `E: file-read-error` against the icon.
    #
    # Unsetting XDG_RUNTIME_DIR just for appstreamcli`s subprocess gives glycin
    # nothing to forward, so the sandbox`s own /tmp-run setenv stands. The
    # parent (flatpak-builder) keeps its XDG_RUNTIME_DIR untouched.
    #
    # This is arguably a bug in glycin (it shouldn`t clobber its own sandbox
    # env with the parent`s); link to upstream if/when one is filed.
    #
    # The argv-logging block was diagnostic and is now redundant; leaving it
    # in temporarily so the next CI run still shows the intercepted call for
    # the record. Remove the logging once we`ve confirmed the fix sticks.
    mkdir -p /tmp/bin
    cat > /tmp/bin/appstreamcli <<"WRAPPER_EOF"
#!/bin/bash
{
  printf "================================================================\n"
  printf "APPSTREAMCLI WRAPPER intercepted call with %d arguments:\n" "$#"
  printf "  argv[0]: %q\n" "$0"
  i=1
  for arg in "$@"; do
    printf "  argv[%d]: %q\n" "$i" "$arg"
    i=$((i+1))
  done
  printf "(parent XDG_RUNTIME_DIR before unset: %q)\n" "${XDG_RUNTIME_DIR-(unset)}"
  printf "(verifying env -u actually unsets it for the child:)\n"
  env -u XDG_RUNTIME_DIR bash -c 'printf "  child sees XDG_RUNTIME_DIR=%q\n" "${XDG_RUNTIME_DIR-(unset)}"'
  printf "(injecting --verbose after compose so we capture the bwrap spawn)\n"
  printf "================================================================\n"
} >&2

# Inject --verbose right after the "compose" subcommand so glycin's bwrap
# command lands in stderr and we can confirm whether the second
# "--setenv XDG_RUNTIME_DIR ..." is gone after the env -u strip.
new_args=()
for arg in "$@"; do
  new_args+=("$arg")
  if [[ "$arg" == "compose" ]]; then
    new_args+=("--verbose")
  fi
done

exec env -u XDG_RUNTIME_DIR /usr/bin/appstreamcli "${new_args[@]}"
WRAPPER_EOF
    chmod +x /tmp/bin/appstreamcli
    export PATH=/tmp/bin:$PATH

    flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

    case "$FORMAT" in
      all)
        make deb rpm appimage RELEASE_DIR=/tmp/build
        make flatpak
        ;;
      deb|rpm|appimage)
        make "$FORMAT" RELEASE_DIR=/tmp/build
        ;;
      flatpak)
        make flatpak
        ;;
    esac
  '
echo "==> artifacts in dist/:"
ls -1 dist/*.deb dist/*.rpm dist/*.AppImage dist/*.flatpak 2>/dev/null || true
