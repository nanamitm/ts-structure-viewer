cmake_minimum_required(VERSION 3.21)
project(ts-structure-viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Standalone prototype for the TS structure viewer (viewport zoom/pan timeline).
# Build:
#   cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 \
#         -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64
#   cmake --build build-msvc --config Release
# Run (Qt bin on PATH):
#   $env:PATH="C:\Qt\6.11.1\msvc2022_64\bin;"+$env:PATH
#   .\build-msvc\Release\ts-structure-viewer.exe
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(ts-structure-viewer
    src/main.cpp
    src/StructureViewer.h
    src/StructureViewer.cpp
)
target_link_libraries(ts-structure-viewer PRIVATE Qt6::Widgets)

add_executable(ts-compare-viewer
    src/compare_main.cpp
    src/CompareViewer.h
    src/CompareViewer.cpp
)
target_link_libraries(ts-compare-viewer PRIVATE Qt6::Widgets)

# Real-file inspector: scans an actual .ts (TsScan) and shows structure / PSI /
# timing / picture types. Open a second file to switch into source-vs-export
# compare mode (PSI diff + stacked timing). Reuses the StructureViewer widget.
add_executable(ts-inspector
    src/inspector_main.cpp
    src/TsScan.h            src/TsScan.cpp
    src/PtsGraphWidget.h    src/PtsGraphWidget.cpp
    src/PicTypeWidget.h     src/PicTypeWidget.cpp
    src/StructureViewer.h   src/StructureViewer.cpp
)
target_link_libraries(ts-inspector PRIVATE Qt6::Widgets)

if (WIN32)
    set_target_properties(ts-structure-viewer PROPERTIES WIN32_EXECUTABLE ON)
    set_target_properties(ts-compare-viewer PROPERTIES WIN32_EXECUTABLE ON)
    set_target_properties(ts-inspector PROPERTIES WIN32_EXECUTABLE ON)
endif()
