cmake_minimum_required(VERSION 3.25)

# Also support building the launcher without generated game code or game files.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(broken_bond_launcher LANGUAGES CXX)
    find_package(rexglue CONFIG REQUIRED)
endif()

if(NOT WIN32)
    message(FATAL_ERROR "The reused launcher currently supports Windows only")
endif()

enable_language(RC)
configure_file(src/launcher.rc.in "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/favicon.ico")

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/thirdparty/imgui")
add_executable(broken_bond_launcher WIN32
    "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc"
    src/main.cpp
    src/config_manager.cpp
    src/game_files.cpp
    src/localization.cpp
    src/texture_loader.cpp
    src/theme.cpp
    src/ui.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_compile_features(broken_bond_launcher PRIVATE cxx_std_23)
target_include_directories(broken_bond_launcher PRIVATE
    src ${IMGUI_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/thirdparty/stb")
target_link_libraries(broken_bond_launcher PRIVATE
    SDL3::SDL3-static rex::tomlplusplus d3d11 dxgi bcrypt)
rexglue_apply_target_settings(broken_bond_launcher)
if(MSVC)
    target_compile_options(broken_bond_launcher PRIVATE /utf-8)
endif()
set_target_properties(broken_bond_launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_custom_target(broken_bond_launcher_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:broken_bond_launcher>/assets")
add_dependencies(broken_bond_launcher broken_bond_launcher_assets)

# Explicit regression target; excluded from ordinary game builds.
add_executable(broken_bond_launcher_test EXCLUDE_FROM_ALL
    tests/test_launcher.cpp src/config_manager.cpp src/game_files.cpp)
target_compile_features(broken_bond_launcher_test PRIVATE cxx_std_23)
target_include_directories(broken_bond_launcher_test PRIVATE src)
target_link_libraries(broken_bond_launcher_test PRIVATE rex::tomlplusplus bcrypt)
set_target_properties(broken_bond_launcher_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
