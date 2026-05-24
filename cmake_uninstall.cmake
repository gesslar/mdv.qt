# Reverses `cmake --install` by removing every file it recorded in
# install_manifest.txt. Invoked by `make uninstall`. Portable: CMake reads the
# manifest and removes each path, so it behaves the same on Linux/macOS/Windows
# without relying on a platform shell.
#
#   cmake -DMANIFEST=<build-dir>/install_manifest.txt -P cmake_uninstall.cmake
#
# The manifest stores absolute paths as actually installed, so uninstall hits
# the right location regardless of the PREFIX used at install time. Empty
# directories are intentionally left behind — they're often shared (e.g.
# icons/hicolor/...) and not ours to remove.

if(NOT DEFINED MANIFEST)
    message(FATAL_ERROR "MANIFEST not set — run this via `make uninstall`.")
endif()

if(NOT EXISTS "${MANIFEST}")
    message(STATUS
        "Nothing to uninstall: ${MANIFEST} not found. "
        "Run `make install` first.")
    return()
endif()

file(STRINGS "${MANIFEST}" installed_files)

foreach(file IN LISTS installed_files)
    if(EXISTS "${file}" OR IS_SYMLINK "${file}")
        message(STATUS "Removing ${file}")
        file(REMOVE "${file}")
    else()
        message(STATUS "Already gone: ${file}")
    endif()
endforeach()
