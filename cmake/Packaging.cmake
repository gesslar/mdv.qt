# CPack configuration — builds the OS-native installer from the very same
# install() rules `make install` uses (binary + runtime deps + desktop bits).
# Linux today produces .deb / .rpm; Windows produces an NSIS .exe installer.
# macOS will follow its own (non-CPack) path; see work/macos-packaging.md.
#
# Driven from the per-host Makefile.dist.* include:
#     Linux:   make deb / make rpm / make dist
#     Windows: make exe / make dist  (NSIS)
#
# This file is include()d at the very end of CMakeLists.txt, AFTER the install()
# rules — CPack snapshots those rules, so whatever `make install` would lay down
# is exactly what lands inside the packages.

# Common metadata. Both per-OS branches reuse these; keeps the package's
# name/version/vendor/contact consistent across formats.
set(CPACK_PACKAGE_NAME mdv)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION
    "mdv is a fast, offline desktop viewer for Markdown files, with syntax-\
highlighted code blocks and themeable rendering.")
set(CPACK_PACKAGE_VENDOR gesslar)
set(CPACK_PACKAGE_CONTACT "Brian M. Workman <bmw@gesslar.dev>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gesslar/mdv.qt")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")

if(UNIX AND NOT APPLE)
    # Packages install under /usr (not CMake's /usr/local default) so the binary
    # is /usr/bin/mdv and the .desktop/icon sit at the standard XDG paths the
    # desktop environment actually scans.
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)

    # --- Debian (.deb) --------------------------------------------------------
    #
    # Runtime deps are hand-listed with Debian/Ubuntu package names. We do NOT
    # use CPACK_DEBIAN_PACKAGE_SHLIBDEPS: it shells out to dpkg-shlibdeps
    # (dpkg-dev), and run on a non-Debian host it resolves THIS distro's soname
    # packages — wrong names for a .deb. Keep this list in step with the link
    # line in CMakeLists.txt (DEVELOPMENT.md lists the matching dev packages).
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6widgets6 (>= 6.5), libmd4c0, libkf6syntaxhighlighting6")
    set(CPACK_DEBIAN_PACKAGE_SECTION text)
    set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    # --- RPM (.rpm) -----------------------------------------------------------
    #
    # rpmbuild's automatic dependency generator derives Requires from the linked
    # .so files, and it's run on/for an RPM distro so those soname deps are
    # correct — no hand-listing needed here.
    set(CPACK_RPM_PACKAGE_LICENSE "0BSD")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Text")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    # CPackRPM doesn't fall back to CPACK_PACKAGE_DESCRIPTION (the DEB generator
    # does), so without this the %description is CPack's "created using CPack"
    # boilerplate. Point it at the same text the .deb uses.
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")

    include(CPack)
endif()

if(WIN32)
    # No CPack on Windows. CPack's bundled NSIS template hard-codes
    # `RequestExecutionLevel admin`, which means every install asks for UAC
    # whether the user wants a per-user install or not. We want the user to
    # *choose* between per-user (no admin, AppData) and per-machine (admin,
    # Program Files) via NSIS's MultiUser.nsh — same posture electron-builder
    # exposes with oneClick:false + allowElevation:true. CPack's template
    # isn't parameterisable enough to layer that on, so we drive makensis
    # directly against our own slim cmake/mdv.nsi.in template.
    #
    # Pipeline:
    #   1. cmake --install $RELEASE_DIR --prefix <stage>
    #      → stages mdv.exe + Qt runtime + plugins + icon under <stage>
    #        using the install() rules in the main CMakeLists.txt
    #   2. configure_file(cmake/mdv.nsi.in build/mdv.nsi)
    #      → bakes PROJECT_VERSION / vendor / homepage etc. into the script
    #   3. makensis -DSTAGE_DIR=<stage> -DOUTFILE=<artifact> build/mdv.nsi
    #      → produces the .exe
    #
    # The package_nsis target wires all three into a single cmake --build
    # invocation. Driven from Makefile.dist.windows.
    find_program(MAKENSIS_EXECUTABLE makensis
        HINTS "$ENV{ProgramFiles\(x86\)}/NSIS" "$ENV{ProgramFiles}/NSIS"
        PATH_SUFFIXES Bin)
    if(NOT MAKENSIS_EXECUTABLE)
        message(WARNING
            "makensis not found — `make exe` will fail. Install NSIS 3.x "
            "(choco install nsis) and ensure NSIS\\Bin is on PATH.")
    endif()

    set(MDV_NSIS_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mdv.nsi.in")
    set(MDV_NSIS_SCRIPT   "${CMAKE_BINARY_DIR}/mdv.nsi")
    set(MDV_NSIS_STAGE    "${CMAKE_BINARY_DIR}/_nsi_stage")
    # MDV_PACKAGE_OUTPUT_DIR is set by Makefile.dist.windows to the project's
    # DIST_DIR (dist/). Default to the build dir so manual cmake --build
    # invocations land somewhere sensible.
    if(NOT MDV_PACKAGE_OUTPUT_DIR)
        set(MDV_PACKAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
    endif()
    set(MDV_PACKAGE_FILE
        "${MDV_PACKAGE_OUTPUT_DIR}/mdv-${PROJECT_VERSION}-win64.exe")

    # The .ico and license paths get baked into the script. configure_file()
    # writes the .nsi at configure time; cache invalidation is automatic when
    # the .in template or any @VAR@ changes.
    set(MDV_ICON_PATH    "${CMAKE_SOURCE_DIR}/resources/icons/mdv.ico")
    set(MDV_LICENSE_PATH "${CMAKE_SOURCE_DIR}/LICENSE.txt")
    configure_file("${MDV_NSIS_TEMPLATE}" "${MDV_NSIS_SCRIPT}" @ONLY)

    add_custom_target(package_nsis
        # Wipe-and-fill the staging dir so leftover files from a previous run
        # don't sneak into the installer.
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${MDV_NSIS_STAGE}"
        COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                --prefix "${MDV_NSIS_STAGE}" --config Release
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MDV_PACKAGE_OUTPUT_DIR}"
        COMMAND "${MAKENSIS_EXECUTABLE}"
                "-DSTAGE_DIR=${MDV_NSIS_STAGE}"
                "-DOUTFILE=${MDV_PACKAGE_FILE}"
                "${MDV_NSIS_SCRIPT}"
        DEPENDS mdv
        COMMENT "Packaging mdv ${PROJECT_VERSION} as NSIS installer"
        VERBATIM)
endif()
