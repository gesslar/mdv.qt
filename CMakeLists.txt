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
)

target_link_libraries(mdv PRIVATE Qt6::Widgets)

target_include_directories(mdv PRIVATE src)

set_target_properties(mdv PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
