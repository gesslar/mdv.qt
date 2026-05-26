cmake_minimum_required(VERSION 3.21)

project(mdv
    VERSION 2.0.0
    DESCRIPTION "An offline Markdown viewer for everybody! For free! Huzzah!"
    LANGUAGES CXX
)

# Single source of the app's identity. project() above owns the version and the
# name (PROJECT_VERSION / PROJECT_NAME); MDV_ORG is the one extra string. These
# feed the binary (compile defs), CPack (cmake/Packaging.cmake), and the
# AppStream metainfo (configure_file) — so cutting a release is a one-line bump
# of VERSION above, nowhere else.
set(MDV_ORG "gesslar")
# AppStream release date = the commit date of the built ref. Stable for a given
# source state (rebuilding never moves it) and, for a CD release built from a
# tag, it IS the tag date. Falls back to the build date only when git is
# unavailable (e.g. a source tarball with no .git).
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%cd --date=short
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE MDV_RELEASE_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(NOT MDV_RELEASE_DATE)
    string(TIMESTAMP MDV_RELEASE_DATE "%Y-%m-%d" UTC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Note: AUTOMOC/AUTOUIC/AUTORCC are enabled per-target on `mdv` (below), not
# globally, so they don't leak into the FetchContent'd md4c build.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

# --- Non-Qt dependencies ------------------------------------------------------
#
# Acquisition rule for build-time deps: take them from the system package
# manager wherever it has them (Linux/macOS), and only build in-tree where the
# platform has no package (Windows md4c). Distribution linkage — which .so the
# .deb pulls, which .dll the installer bundles — is a packaging concern handled
# downstream, NOT here. DEVELOPMENT.md has the per-platform install steps.

# md4c (CommonMark + GFM markdown parser). We use the C library directly
# rather than QTextDocument::setMarkdown because the latter bakes explicit
# character formats into the document that override CSS supplied via
# setDefaultStyleSheet.
#
# The system package exports the namespaced imported target md4c::md4c-html;
# FetchContent exports the plain target md4c-html. The ${MD4C} indirection
# papers over that difference so the link line stays uniform.
if(WIN32)
    # FetchContent'd subprojects (md4c, KSH, ECM) all call install() at
    # configure time. Those rules bake into cmake_install.cmake and CPack
    # would otherwise ship md4c headers, KSH .cmake configs, etc. as part of
    # the mdv installer. The clean way to silence them is to override the
    # built-in install() command with a gated forwarder: by default it's a
    # no-op, and we flip the gate ON only for our own install rules below.
    #
    # Redefining install() makes the original built-in reachable as _install
    # (CMake renames the prior definition under that underscore name). One
    # level of shadowing is reliable; doing it more than once gets ambiguous,
    # which is why we use a single override + a flag rather than re-overriding.
    set(_mdv_install_enabled OFF)
    function(install)
        if(_mdv_install_enabled)
            _install(${ARGV})
        endif()
    endfunction()

    # No dnf/brew/vcpkg-by-default for md4c here — fetch & build it just to dev.
    include(FetchContent)
    set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        md4c
        GIT_REPOSITORY https://github.com/mity/md4c.git
        GIT_TAG release-0.5.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(md4c)
    set(MD4C md4c-html)
else()
    find_package(md4c REQUIRED)
    set(MD4C md4c::md4c-html)
endif()

# KSyntaxHighlighting — code-block tokenizer; we map its default styles onto
# the active content theme's syntax.* colors.
#
# Same acquisition rule as md4c: system package on Linux/macOS, FetchContent on
# Windows where there's no package. KSH is a KDE Framework, so the Windows
# branch also has to FetchContent extra-cmake-modules (ECM) first — KSH does
# find_package(ECM) at configure time. The KSH build also runs the
# katehighlightingindexer host tool and lrelease, which is why this is a
# *native* Windows path only — cross-compiling KSH from Linux trips on those
# host tools (they get built for the Windows target and can't run on the Linux
# host that's doing the build).
#
# As with md4c, the imported target names differ between paths:
# find_package gives `KF6::SyntaxHighlighting`; the in-build FetchContent gives
# the same namespaced alias (KDE Frameworks add it via add_library(ALIAS)).
# The ${KSH} indirection keeps the link line uniform regardless.
if(WIN32)
    # KSH pulls Qt6::Network transitively. On Linux/macOS the system
    # find_package(KF6SyntaxHighlighting) drags it in via the exported config's
    # find_dependency(); on the FetchContent path the consumer (this project)
    # has to declare it. Add it once here so the warning goes away and the
    # link line is honest.
    find_package(Qt6 6.5 REQUIRED COMPONENTS Network)

    include(FetchContent)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    # XercesC is an optional KSH dep used for richer XML validation; we don't
    # need it, and skipping the find_package avoids a "not found" warning on
    # boxes without it. Translations + the katehighlightingindexer build and
    # run natively here, so they don't need disabling.
    #
    # KSH's CMakeLists also hard-codes add_subdirectory(examples) and
    # add_subdirectory(src/cli) with no off-switch — they build alongside the
    # library. They don't affect the mdv.exe link and their install() rules
    # are suppressed below, so we just absorb the extra build time.
    set(CMAKE_DISABLE_FIND_PACKAGE_XercesC ON CACHE BOOL "" FORCE)

    # --- extra-cmake-modules ----------------------------------------------
    #
    # KSH's CMakeLists does find_package(ECM NO_MODULE REQUIRED) plus a
    # batch of include(ECM*). FetchContent_MakeAvailable() on ECM by itself
    # is not enough: ECM's build-tree ECMConfig.cmake sets its module path
    # to the eventual install location (which doesn't exist), so the
    # subsequent include() calls in KSH fail to find ECMGenerateHeaders etc.
    #
    # ECM is a pure-CMake "no compile" project — installing it is just a
    # file copy. So we clone, configure, and install it to a local prefix
    # inside the build dir, then add that prefix to CMAKE_PREFIX_PATH. From
    # KSH's point of view ECM looks system-installed and everything resolves.
    #
    # We cache the install (gate on ECMConfig.cmake's existence) so reconfigures
    # don't re-clone.
    set(ECM_LOCAL_PREFIX "${CMAKE_BINARY_DIR}/_ecm-prefix")
    if(NOT EXISTS "${ECM_LOCAL_PREFIX}/share/ECM/cmake/ECMConfig.cmake")
        set(_ecm_src "${CMAKE_BINARY_DIR}/_ecm-src")
        set(_ecm_bld "${CMAKE_BINARY_DIR}/_ecm-build")
        if(NOT EXISTS "${_ecm_src}/CMakeLists.txt")
            find_package(Git REQUIRED)
            file(REMOVE_RECURSE "${_ecm_src}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" clone --depth 1
                    --branch v6.26.0
                    https://invent.kde.org/frameworks/extra-cmake-modules.git
                    "${_ecm_src}"
                RESULT_VARIABLE _ecm_clone)
            if(NOT _ecm_clone EQUAL 0)
                message(FATAL_ERROR "ECM clone failed (exit ${_ecm_clone})")
            endif()
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                -S "${_ecm_src}" -B "${_ecm_bld}"
                -G "${CMAKE_GENERATOR}"
                "-DCMAKE_INSTALL_PREFIX=${ECM_LOCAL_PREFIX}"
                -DBUILD_TESTING=OFF
            RESULT_VARIABLE _ecm_cfg)
        if(NOT _ecm_cfg EQUAL 0)
            message(FATAL_ERROR "ECM configure failed (exit ${_ecm_cfg})")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --install "${_ecm_bld}"
            RESULT_VARIABLE _ecm_install)
        if(NOT _ecm_install EQUAL 0)
            message(FATAL_ERROR "ECM install failed (exit ${_ecm_install})")
        endif()
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${ECM_LOCAL_PREFIX}")

    # --- Perl (KSH build-time dep) ----------------------------------------
    #
    # KSH runs a Perl script during configure (the regex tester). Windows
    # doesn't ship Perl, but Git for Windows bundles one at
    # <git-prefix>/usr/bin/perl.exe, which the user already has installed if
    # they got here. Probe for it and seed PERL_EXECUTABLE if not on PATH,
    # so KSH's find_package(Perl) succeeds without the user having to install
    # Strawberry Perl separately. The CI side can `choco install strawberryperl`
    # if it'd rather have a dedicated one — this stays a fallback.
    find_program(PERL_EXECUTABLE perl)
    if(NOT PERL_EXECUTABLE)
        find_package(Git QUIET)
        if(GIT_EXECUTABLE)
            get_filename_component(_git_bin "${GIT_EXECUTABLE}" DIRECTORY)
            get_filename_component(_git_prefix "${_git_bin}" DIRECTORY)
            find_program(PERL_EXECUTABLE perl
                HINTS "${_git_prefix}/usr/bin"
                NO_DEFAULT_PATH)
        endif()
    endif()
    if(NOT PERL_EXECUTABLE)
        message(FATAL_ERROR
            "Perl is required to build KSyntaxHighlighting on Windows. "
            "Install Strawberry Perl (choco install strawberryperl) or "
            "Git for Windows (which bundles a Perl), and reconfigure.")
    endif()

    # --- KSyntaxHighlighting ----------------------------------------------
    #
    # Pinned to v6.26.0 so the API matches the Linux/macOS find_package side
    # (system KF6 is 6.26.0 on the dev box). With ECM resolvable from the
    # prefix above, KSH builds + installs its data resources into the lib,
    # so the deployed DLL travels with all syntax/theme defs baked in.
    FetchContent_Declare(
        KSyntaxHighlighting
        GIT_REPOSITORY https://invent.kde.org/frameworks/syntax-highlighting.git
        GIT_TAG v6.26.0
        GIT_SHALLOW TRUE
    )
    # install() is still gated OFF here (set at the top of this if(WIN32)
    # branch), so KSH's install rules — like md4c's above — become no-ops.
    FetchContent_MakeAvailable(KSyntaxHighlighting)
    # KSH only adds the KF6:: namespaced alias on *install* (the exported
    # config does add_library(KF6::SyntaxHighlighting IMPORTED)). In-tree
    # the target is just `KF6SyntaxHighlighting`. Add the alias ourselves so
    # the ${KSH} indirection points at the same name on every platform.
    if(NOT TARGET KF6::SyntaxHighlighting)
        add_library(KF6::SyntaxHighlighting ALIAS KF6SyntaxHighlighting)
    endif()
    set(KSH KF6::SyntaxHighlighting)
else()
    # The Qt installer doesn't bundle KF6, so off Linux/macOS-with-MacPorts the
    # user supplies it via -DCMAKE_PREFIX_PATH=<prefix>. See DEVELOPMENT.md.
    find_package(KF6SyntaxHighlighting REQUIRED)
    set(KSH KF6::SyntaxHighlighting)
endif()

qt_standard_project_setup()

qt_add_executable(mdv
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/DocumentView.cpp
    src/DocumentView.h
    src/EditorGroup.cpp
    src/EditorGroup.h
    src/EditorArea.cpp
    src/EditorArea.h
    src/TabBar.cpp
    src/TabBar.h
    src/Theme.cpp
    src/Theme.h
    src/ContentTheme.cpp
    src/ContentTheme.h
    src/Markdown.cpp
    src/Markdown.h
    src/Highlighter.cpp
    src/Highlighter.h
    src/PreferencesDialog.cpp
    src/PreferencesDialog.h
    resources/resources.qrc
)

# Windows app icon: embed resources/mdv.rc → mdv.ico into the executable so
# Explorer, the taskbar, Start Menu shortcuts, and the Apps & Features entry
# render the app icon (without this they fall back to the generic exe icon).
# CMake autoselects windres on MinGW / rc.exe on MSVC.
if(WIN32)
    target_sources(mdv PRIVATE resources/mdv.rc)
endif()

target_link_libraries(mdv PRIVATE Qt6::Widgets ${MD4C} ${KSH})

target_include_directories(mdv PRIVATE src)

# Bake the identity into the binary so main.cpp doesn't re-hardcode it.
target_compile_definitions(mdv PRIVATE
    MDV_ORG="${MDV_ORG}"
    MDV_APP="${PROJECT_NAME}"
    MDV_VERSION="${PROJECT_VERSION}")

# Qt autogen is scoped to this target (rather than set globally) so the
# FetchContent md4c build doesn't inherit AUTOMOC — which would emit a stray
# C++ moc unit and trip md4c's C-only -Wdeclaration-after-statement flag.
set_target_properties(mdv PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# Linux desktop integration: install the launcher, AppStream metainfo, and a
# themed icon so the Wayland/X11 compositor can match the running window
# (app_id "dev.gesslar.mdv") to its icon, and software centers can list the app.
# On Wayland the dock icon comes ONLY from this .desktop match, never from
# QApplication::setWindowIcon(). No-op for Windows/macOS packaging.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)

    # Generate the metainfo with version/date filled from the single source above.
    configure_file(resources/dev.gesslar.mdv.metainfo.xml.in
        ${CMAKE_CURRENT_BINARY_DIR}/dev.gesslar.mdv.metainfo.xml @ONLY)

    install(TARGETS mdv RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES resources/dev.gesslar.mdv.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dev.gesslar.mdv.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES resources/icons/mdv.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
        RENAME dev.gesslar.mdv.png)
endif()

# Windows install layout follows the Qt deploy convention:
#   <prefix>/bin/mdv.exe  + Qt6*.dll + mingw runtime DLLs + qt.conf
#   <prefix>/plugins/{platforms,styles,imageformats,...}
#   <prefix>/translations/qt_*.qm
# qt.conf in bin/ has `Prefix = ..` so the Qt runtime walks back up to find
# plugins/ and translations/. windeployqt produces exactly this shape;
# qt_generate_deploy_app_script() runs windeployqt at install time so CPack's
# staging step picks it up. CPack's NSIS template auto-points the Start Menu
# / desktop shortcut at $INSTDIR\bin\mdv.exe to match.
if(WIN32)
    # Flip the install() gate ON — until now it's been a no-op so md4c/KSH/ECM
    # didn't bake their install rules into our package. Our own install rules
    # need the real install() built-in (reachable as _install thanks to the
    # function override above).
    set(_mdv_install_enabled ON)
    install(TARGETS mdv RUNTIME DESTINATION bin)
    install(FILES resources/icons/mdv.ico DESTINATION bin)
    qt_generate_deploy_app_script(
        TARGET mdv
        OUTPUT_SCRIPT _mdv_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${_mdv_deploy_script})
endif()

# CPack: turn the install() rules above into .deb / .rpm. Kept in its own file
# and included last (after the install rules it packages). Linux-only today;
# see cmake/Packaging.cmake and Makefile.dist.linux.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake)
