cmake_minimum_required(VERSION 3.21)

project(mdv
    VERSION 0.1.0
    DESCRIPTION "An offline Markdown viewer for everybody! For free! Huzzah!"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

# md4c (CommonMark + GFM markdown parser). We use the C library directly
# rather than QTextDocument::setMarkdown because the latter bakes explicit
# character formats into the document that override CSS supplied via
# setDefaultStyleSheet.
find_package(PkgConfig REQUIRED)
pkg_check_modules(MD4C_HTML REQUIRED IMPORTED_TARGET md4c-html)

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
    src/PreferencesDialog.cpp
    src/PreferencesDialog.h
    resources/resources.qrc
)

target_link_libraries(mdv PRIVATE Qt6::Widgets PkgConfig::MD4C_HTML)

target_include_directories(mdv PRIVATE src)

set_target_properties(mdv PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
