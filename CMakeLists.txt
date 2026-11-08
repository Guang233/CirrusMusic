cmake_minimum_required(VERSION 3.24)

project(CirrusMusic VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Multimedia)

qt_standard_project_setup(REQUIRES 6.10)

include(CTest)
add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

# QML modules, the product entry (appCirrusMusic) and the gallery; see docs/ui/README.md.
add_subdirectory(ui)

option(CIRRUS_BUILD_AUDIO_POC "Build the audio engine PoC (tools/audio-poc, needs FFmpeg)" OFF)
if(CIRRUS_BUILD_AUDIO_POC)
    add_subdirectory(tools/audio-poc)
endif()
