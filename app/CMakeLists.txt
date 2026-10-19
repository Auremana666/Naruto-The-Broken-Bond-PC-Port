# narutobb - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(narutobb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(NARUTOBB_SOURCES
    src/main.cpp
    src/usbcam.cpp
    src/options_menu.cpp
    src/crash_handler.cpp
    src/fps_counter.cpp
    src/intro_skip.cpp
    src/pad_input.cpp
    src/player_assignment.cpp
    src/input_layout.cpp
    src/unlock_all.cpp
    src/auto_jutsu.cpp
    src/title_screen.cpp
)

if(WIN32)
    # Program icon (the Sharingan Woo picked) and the title screen pictures.
    enable_language(RC)
    list(APPEND NARUTOBB_SOURCES assets/icon/naruto.rc)
    # The title screen's hidden extra, only when its files are present (not in the public source).
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/secret/secret.rc)
        list(APPEND NARUTOBB_SOURCES assets/secret/secret.rc)
    endif()
    add_executable(narutobb WIN32 ${NARUTOBB_SOURCES})
else()
    add_executable(narutobb ${NARUTOBB_SOURCES})
endif()

rexglue_setup_target(narutobb GPU_PLUGINS xenos)
# The SDK runtime is built with perf counters on (the F3 overlay needs them), and that flag changes
# the layout of overlay classes that rex_app.cpp, compiled into this target, allocates. Without it
# here, F3 overruns the debug overlay object and corrupts the heap.
target_compile_definitions(narutobb PRIVATE REXGLUE_ENABLE_PERF_COUNTERS)
target_link_libraries(narutobb PRIVATE dbghelp psapi winmm)

# Runtime config next to the executable.
add_custom_target(narutobb_runtime_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/naruto.toml
            $<TARGET_FILE_DIR:narutobb>/naruto.toml)
add_dependencies(narutobb narutobb_runtime_config)



set_target_properties(narutobb PROPERTIES OUTPUT_NAME "Naruto The Broken Bond PC")
