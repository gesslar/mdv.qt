cmake_minimum_required(VERSION 3.21)

project(mdv
    VERSION 0.1.0
    DESCRIPTION "An offline Markdown viewer for everybody! For free! Huzzah!"
    LANGUAGES CXX
)

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
# the active content theme's syntax.* colors. find_package resolves it on
# every platform, but the Qt installer doesn't bundle KF6: on Windows install
# it via KDE Craft and on macOS via MacPorts/Craft, then point CMake at it with
# -DCMAKE_PREFIX_PATH=<prefix>. See DEVELOPMENT.md for the per-platform steps.
find_package(KF6SyntaxHighlighting REQUIRED)

qt_standard_project_setup()

qt_add_executable(mdv
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/DocumentView.cpp
    src/DocumentView.h
    src/EditorPane.cpp
    src/EditorPane.h
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

target_link_libraries(mdv PRIVATE Qt6::Widgets ${MD4C} KF6::SyntaxHighlighting)

target_include_directories(mdv PRIVATE src)

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

# Linux desktop integration: install the launcher and a themed icon so the
# Wayland/X11 compositor can match the running window (app_id "mdv") to its
# icon. On Wayland the dock icon comes ONLY from this .desktop match, never
# from QApplication::setWindowIcon(). No-op for Windows/macOS packaging.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS mdv RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES resources/mdv.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/icons/mdv.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
        RENAME mdv.png)
endif()
