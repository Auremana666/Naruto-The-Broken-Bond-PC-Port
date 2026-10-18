# broken_bond - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(broken_bond LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(BROKENBOND_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(broken_bond WIN32 ${BROKENBOND_SOURCES})
else()
    add_executable(broken_bond ${BROKENBOND_SOURCES})
endif()

rexglue_setup_target(broken_bond GPU_PLUGINS xenos)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_link_options(broken_bond PRIVATE -Xlinker /DEBUG)
endif()

option(BROKENBOND_BUILD_LAUNCHER "Build the Windows launcher" ${WIN32})
if(BROKENBOND_BUILD_LAUNCHER)
    add_subdirectory(launcher)
endif()
