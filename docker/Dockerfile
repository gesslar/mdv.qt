# Reproducible Linux build environment for mdv — produces the .deb, .rpm,
# AppImage, and Flatpak from a clean, pinned Fedora userspace, so a release
# doesn't depend on a dev box's state. See work/appimage-flatpak-docker-cleanroom.md.
#
# This image is the TOOLCHAIN ONLY. The source is mounted at /work at run time,
# and the build just calls the same `make` targets you'd run on the host — so
# the container can't drift from the documented build.
#
#   make linux           # build the image (if needed) + run all formats as you
#
# Note: a Fedora base means the same recent glibc + RELR libs as the host, so the
# AppImage's glibc floor is unchanged. The win is reproducibility + CI, not
# old-distro portability. The AppImage stays patchelf-free (scripts/appimage.sh).
#
# Pinned to the HOST's Fedora release. The AppImage bundles this base's libs but
# loads host-provided ones (libselinux, glibc, ...) at run time; if the base were
# OLDER than the run host, a host lib could demand symbols the bundled (older) lib
# lacks and the app aborts. Matching the host avoids that skew.
#
# --platform=linux/amd64 pins the image to x86_64 regardless of build host: a
# no-op on x86_64, and on an arm64 host (Docker Desktop on Apple Silicon) it
# forces x86_64 so the pinned-x86_64 toolchain (appimagetool, artifact names)
# can't mismatch the host. The build deterministically targets x86_64.
FROM --platform=linux/amd64 fedora:44

# Build deps (DEVELOPMENT.md's Fedora list) + packaging tooling + git (the
# metainfo derives its release date from `git log`) + the appdir-lint deps
# (desktop-file-validate, appstreamcli, mimetype, which).
RUN dnf install -y --setopt=install_weak_deps=False \
      qt6-qtbase-devel kf6-syntax-highlighting-devel md4c-devel \
      extra-cmake-modules cmake ninja-build gcc-c++ git \
      flatpak flatpak-builder dpkg rpm-build \
      desktop-file-utils appstream perl-File-MimeInfo file which curl \
 && dnf clean all

# appimagetool, pinned to a tagged release + verified by sha256 so the
# "reproducible" image actually is (continuous is a rolling tag, and an
# unverified download is a supply-chain gap). It only PACKAGES the AppDir —
# the dependency deploy is hand-rolled patchelf-free in scripts/appimage.sh.
RUN curl -fsSL -o /usr/local/bin/appimagetool \
      https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
 && echo "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0  /usr/local/bin/appimagetool" \
      | sha256sum -c - \
 && chmod +x /usr/local/bin/appimagetool
# AppImages can't FUSE-mount inside a container; extract-and-run instead.
ENV APPIMAGE_EXTRACT_AND_RUN=1

# Flathub remote so flatpak-builder can pull the org.kde.Platform runtime/SDK.
RUN flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

WORKDIR /work
