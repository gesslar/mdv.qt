cmake_minimum_required(VERSION 3.21)

project(mdv
    VERSION 2.4.0
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

# The *Private components are pulled in for QWindowKit (below): its frameless
# contexts compile against Qt's private/QPA headers. QWindowKit finds those
# private targets inside a function scope (via qmsetup), so the imported
# Qt6::*Private targets aren't visible when Qt finalizes the mdv target — which
# prints "<X>Private ... mentioned as a dependency ... but not declared"
# warnings (and potentially incomplete linking) as it walks QWKCore's /
# QWindowKit::Widgets's link interface. Declaring them here at top level makes
# the targets exist project-wide. (On Linux these need qt6-qtbase-private-devel;
# see DEVELOPMENT.md.)
#
# Depending on private Qt API is unavoidable with QWindowKit and is its
# maintainers' burden to track across Qt versions; we knowingly accept it, so
# suppress Qt's per-module "you are using a private module" advisory rather than
# have it fire three times on every configure.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
# Qt6Gui's exported config does find_dependency(WrapVulkanHeaders), which prints
# a "Could NOT find WrapVulkanHeaders" warning on boxes without the Vulkan SDK
# even though Qt builds & runs fine without it (it's an optional Vulkan-QPA
# integration). We never touch Qt's Vulkan path, so skip the find_package
# probe — the warning goes away and Qt links exactly the same.
set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders ON)
find_package(Qt6 6.11 REQUIRED
    COMPONENTS Widgets CorePrivate GuiPrivate WidgetsPrivate)

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
    find_package(Qt6 6.11 REQUIRED COMPONENTS Network)

    include(FetchContent)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    # XercesC is an optional KSH dep used for richer XML validation; we don't
    # need it, and skipping the find_package avoids a "not found" warning on
    # boxes without it. Translations + the katehighlightingindexer build and
    # run natively here, so they don't need disabling.
    set(CMAKE_DISABLE_FIND_PACKAGE_XercesC ON CACHE BOOL "" FORCE)

    # Trim KSH down to just the library + indexer (the parts we link against /
    # need at build time). Three knobs:
    #   1. Qt6Quick disabled → no kquicksyntaxhighlightingplugin (a QML plugin
    #      we never load). This also silences three -Wmissing-include-dirs
    #      warnings the plugin's autogen emits during compile.
    #   2. DISABLE_ALL_OPTIONAL_SUBDIRECTORIES → KSH's examples/ subtrees
    #      (codeeditor, codepdfprinter, minimaltest) default OFF via ECM's
    #      ecm_optional_add_subdirectory. Speeds up clean builds; removes noise.
    #   3. KDE_INSTALL_* preseeded from Qt's GNUInstallDirs values to head off
    #      KDEInstallDirs' four "got its value from CMAKE_INSTALL_*DIR" warnings
    #      (Qt's find_package(Qt6) includes GNUInstallDirs first, then KSH's
    #      include(KDEInstallDirs) flags the cross-include). The defines exist
    #      because of that order, so just adopt them explicitly.
    # The ksyntaxhighlighter6 CLI executable still builds (no upstream off-
    # switch and it's warning-free), but it's not linked into mdv.exe and its
    # install() rules are suppressed.
    set(CMAKE_DISABLE_FIND_PACKAGE_Qt6Quick ON CACHE BOOL "" FORCE)
    set(DISABLE_ALL_OPTIONAL_SUBDIRECTORIES ON CACHE BOOL "" FORCE)
    # ECM's KDEFrameworkCompilerSettings probes for Sphinx (KSH doc builder).
    # We're not building KSH's docs, so skip the probe — silences the
    # "Could NOT find Sphinx" line on every configure.
    set(CMAKE_DISABLE_FIND_PACKAGE_Sphinx ON CACHE BOOL "" FORCE)
    include(GNUInstallDirs)
    foreach(_dir BINDIR SBINDIR LIBEXECDIR SYSCONFDIR SHAREDSTATEDIR
                 LOCALSTATEDIR RUNSTATEDIR LIBDIR INCLUDEDIR OLDINCLUDEDIR
                 DATAROOTDIR DATADIR INFODIR LOCALEDIR MANDIR DOCDIR)
        if(NOT DEFINED KDE_INSTALL_${_dir} AND DEFINED CMAKE_INSTALL_${_dir})
            set(KDE_INSTALL_${_dir} "${CMAKE_INSTALL_${_dir}}" CACHE PATH "")
        endif()
    endforeach()

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
            # OUTPUT/ERROR_VARIABLE swallow git's progress chatter (the
            # "Cloning into ..." + detached-HEAD note + the harmless
            # "refs/tags/v6.26.0 <sha> is not a commit!" warning that git
            # emits on a shallow clone of an annotated-tag ref). On a real
            # failure we surface the captured output via FATAL_ERROR.
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" clone --depth 1 --no-tags
                    --branch v6.26.0
                    https://invent.kde.org/frameworks/extra-cmake-modules.git
                    "${_ecm_src}"
                OUTPUT_VARIABLE _ecm_clone_log
                ERROR_VARIABLE _ecm_clone_log
                RESULT_VARIABLE _ecm_clone)
            if(NOT _ecm_clone EQUAL 0)
                message(FATAL_ERROR
                    "ECM clone failed (exit ${_ecm_clone}):\n${_ecm_clone_log}")
            endif()
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                -S "${_ecm_src}" -B "${_ecm_bld}"
                -G "${CMAKE_GENERATOR}"
                "-DCMAKE_INSTALL_PREFIX=${ECM_LOCAL_PREFIX}"
                -DBUILD_TESTING=OFF
                # ECM is a "no compile" pure-CMake project, so it has no need
                # to probe for Sphinx (or anything else doc-related); skip the
                # find_package(Sphinx) inside its own configure to stop the
                # "Could NOT find Sphinx" line from bleeding into our log.
                -DCMAKE_DISABLE_FIND_PACKAGE_Sphinx=ON
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

# QWindowKit — frameless-window scaffolding (custom titlebar + correct snap
# layouts / resize zones / system buttons). No system package on any platform,
# so we FetchContent it. QWINDOWKIT_INSTALL=OFF keeps its install rules from
# leaking into the .deb/.rpm; on Windows the install() override above would
# already gate them, but the off-switch is cleaner.
include(FetchContent)
set(QWINDOWKIT_BUILD_STATIC ON  CACHE BOOL "" FORCE)
set(QWINDOWKIT_INSTALL      OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    QWindowKit
    GIT_REPOSITORY https://github.com/stdware/qwindowkit.git
    GIT_TAG 1.5.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(QWindowKit)

# Quiet the two -Wdeprecated-declarations from QWKCore's win32windowcontext.cpp
# (QSurface::RasterGLSurface — Qt 6.11 deprecated it in favour of RasterSurface).
# Upstream QWindowKit hasn't tracked the rename in 1.5.0; we don't touch the
# source ourselves, so just suppress the warning on the third-party targets
# instead of patching the FetchContent'd tree. Drop this once we move to a
# QWindowKit release that retires RasterGLSurface.
foreach(_qwk_tgt QWKCore QWKWidgets)
    if(TARGET ${_qwk_tgt})
        target_compile_options(${_qwk_tgt} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-deprecated-declarations>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4996>)
    endif()
endforeach()

qt_standard_project_setup()

qt_add_executable(mdv
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/TitleBar.cpp
    src/TitleBar.h
    src/WindowsChrome.cpp
    src/WindowsChrome.h
    src/CodiconFont.cpp
    src/CodiconFont.h
    src/ChromeStyle.h
    src/DocumentView.cpp
    src/DocumentView.h
    src/FindBar.cpp
    src/FindBar.h
    src/OutlinePanel.cpp
    src/OutlinePanel.h
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
    # dwmapi: DwmSetWindowAttribute for the Win11 immersive-dark / Mica
    # titlebar opt-in. Ships with the OS since Vista; safe to link
    # unconditionally on Windows.
    target_link_libraries(mdv PRIVATE dwmapi)
endif()

target_link_libraries(mdv PRIVATE Qt6::Widgets ${MD4C} ${KSH} QWindowKit::Widgets)

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
