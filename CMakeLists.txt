cmake_minimum_required(VERSION 3.21)
project(ts-inspector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Build:
#   cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 \
#         -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64
#   cmake --build build-msvc --config Release
# The Qt runtime is deployed next to the .exe (windeployqt), so it runs
# standalone:
#   .\build-msvc\Release\ts-inspector.exe "path\to\file.ts"
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Real-file TS inspector: scans an actual .ts (TsScan, on a background thread)
# and shows its structure (GOP/RAP) / streams (PSI) / timing (PTS/DTS/PCR) /
# picture types (I/P/B). Open a second file to switch into source-vs-export
# compare mode (PSI diff + stacked timing).
add_executable(ts-inspector
    src/inspector_main.cpp
    src/TsScan.h            src/TsScan.cpp
    src/TsScanWorker.h      src/TsScanWorker.cpp
    src/PtsGraphWidget.h    src/PtsGraphWidget.cpp
    src/PicTypeWidget.h     src/PicTypeWidget.cpp
    src/StructureViewer.h   src/StructureViewer.cpp
    src/CompareViewer.h     src/CompareViewer.cpp
    src/CompareMap.h        src/CompareMap.cpp
)
target_link_libraries(ts-inspector PRIVATE Qt6::Widgets)

if (WIN32)
    set_target_properties(ts-inspector PROPERTIES WIN32_EXECUTABLE ON)

    # Deploy the Qt runtime DLLs + plugins next to the .exe (windeployqt) so it
    # runs standalone without Qt on PATH.
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ts-inspector POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --no-translations
                    --no-system-d3d-compiler --no-opengl-sw "$<TARGET_FILE:ts-inspector>"
            COMMENT "Deploying Qt runtime for ts-inspector" VERBATIM)
    endif()
endif()
