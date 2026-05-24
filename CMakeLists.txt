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

# Code-block syntax highlighting. Taken from the system on Linux/macOS; on
# Windows this will need to be bundled/built statically at packaging time
# (mirroring the md4c strategy below). KSyntaxHighlighting tokenizes code and
# we map its default styles onto the active content theme's syntax.* colors.
find_package(KF6SyntaxHighlighting REQUIRED)

# md4c (CommonMark + GFM markdown parser). We use the C library directly
# rather than QTextDocument::setMarkdown because the latter bakes explicit
# character formats into the document that override CSS supplied via
# setDefaultStyleSheet.
#
# Fetched and built in-tree so Windows/macOS/Linux all share the same
# dependency story without a system pkg-config install.
include(FetchContent)
# Linkage choice per platform matches how we ship:
#   Windows  — static, so the installer ships a single self-contained mdv.exe.
#   Linux/mac — shared, so .deb/.rpm/Homebrew can swap our in-tree build for
#               a system md4c at package time without changing link semantics.
# Cache + FORCE because md4c's CMakeLists calls option(BUILD_SHARED_LIBS …)
# under CMP0077 OLD, which would otherwise clobber a plain set().
if(WIN32)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
else()
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
endif()
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG release-0.5.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(md4c)

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

target_link_libraries(mdv PRIVATE Qt6::Widgets md4c-html KF6::SyntaxHighlighting)

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
