# CPack configuration — builds .deb and .rpm from the very same install()
# rules the `make install` path uses (the binary, the .desktop launcher, and
# the icon). Linux-only for now; Windows/macOS packaging will get their own
# story when we cross those bridges.
#
# Driven from the Makefile (see Makefile.dist.linux):
#     make deb    → dist/mdv_<version>_<arch>.deb
#     make rpm    → dist/mdv-<version>-1.<arch>.rpm
#     make dist   → both
#
# This file is include()d at the very end of CMakeLists.txt, AFTER the install()
# rules — CPack snapshots those rules, so whatever `make install` would lay down
# is exactly what lands inside the packages.

if(UNIX AND NOT APPLE)
    # Packages install under /usr (not CMake's /usr/local default) so the binary
    # is /usr/bin/mdv and the .desktop/icon sit at the standard XDG paths the
    # desktop environment actually scans.
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)

    set(CPACK_PACKAGE_NAME mdv)
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_DESCRIPTION
        "mdv is a fast, offline desktop viewer for Markdown files, with syntax-\
highlighted code blocks and themeable rendering.")
    set(CPACK_PACKAGE_VENDOR gesslar)
    set(CPACK_PACKAGE_CONTACT "Brian M. Workman <bmw@gesslar.dev>")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gesslar/mdv.qt")

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
