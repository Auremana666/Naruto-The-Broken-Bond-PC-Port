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
    src/app_version.cpp
    src/config_manager.cpp
    src/folders.cpp
    src/game_files.cpp
    src/graphics_devices.cpp
    src/key_bindings.cpp
    src/key_names.cpp
    src/localization.cpp
    src/texture_loader.cpp
    src/theme.cpp
    src/ui.cpp
    src/widgets.cpp
    src/pages/game_files.cpp
    src/pages/key_mapping.cpp
    src/pages/play.cpp
    src/pages/settings.cpp
    src/pages/settings_form.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_compile_features(broken_bond_launcher PRIVATE cxx_std_23)
set(BROKENBOND_VERSION "dev" CACHE STRING "Launcher build version")
target_compile_definitions(broken_bond_launcher PRIVATE BROKENBOND_VERSION="${BROKENBOND_VERSION}")
target_include_directories(broken_bond_launcher PRIVATE
    src ${IMGUI_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/thirdparty/stb")
target_link_libraries(broken_bond_launcher PRIVATE
    SDL3::SDL3-static rex::tomlplusplus d3d11 d3d12 dxgi bcrypt)
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
    tests/test_launcher.cpp src/config_manager.cpp src/game_files.cpp src/key_bindings.cpp)
target_compile_features(broken_bond_launcher_test PRIVATE cxx_std_23)
target_include_directories(broken_bond_launcher_test PRIVATE src)
target_link_libraries(broken_bond_launcher_test PRIVATE rex::tomlplusplus bcrypt shell32)
if(MSVC)
    target_compile_options(broken_bond_launcher_test PRIVATE /utf-8)
endif()
set_target_properties(broken_bond_launcher_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(BROKENBOND_LAUNCHER_NATIVE_RENDERER "Offer native settings for an executable built with the native renderer" ${BROKENBOND_NATIVE_RENDERER})
if(BROKENBOND_LAUNCHER_NATIVE_RENDERER)
    target_compile_definitions(broken_bond_launcher PRIVATE BROKENBOND_LAUNCHER_NATIVE_RENDERER=1)
    target_compile_definitions(broken_bond_launcher_test PRIVATE BROKENBOND_LAUNCHER_NATIVE_RENDERER=1)
endif()

add_executable(broken_bond_key_bindings_test EXCLUDE_FROM_ALL
    tests/test_key_bindings.cpp src/key_bindings.cpp src/key_names.cpp)
target_compile_features(broken_bond_key_bindings_test PRIVATE cxx_std_23)
target_include_directories(broken_bond_key_bindings_test PRIVATE src)
target_link_libraries(broken_bond_key_bindings_test PRIVATE SDL3::SDL3-static)

add_executable(broken_bond_localization_test EXCLUDE_FROM_ALL
    tests/test_localization.cpp src/localization.cpp)
target_compile_features(broken_bond_localization_test PRIVATE cxx_std_23)
target_include_directories(broken_bond_localization_test PRIVATE src)
if(MSVC)
    target_compile_options(broken_bond_localization_test PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME launcher_config COMMAND broken_bond_launcher_test "${CMAKE_CURRENT_BINARY_DIR}/test-data/config")
add_test(NAME launcher_keys COMMAND broken_bond_key_bindings_test)
add_test(NAME launcher_localization COMMAND broken_bond_localization_test)

add_executable(broken_bond_runtime_input_test EXCLUDE_FROM_ALL
    tests/test_runtime_input.cpp src/key_bindings.cpp)
target_compile_features(broken_bond_runtime_input_test PRIVATE cxx_std_23)
target_include_directories(broken_bond_runtime_input_test PRIVATE src)
target_link_libraries(broken_bond_runtime_input_test PRIVATE rex::runtime)
rexglue_apply_target_settings(broken_bond_runtime_input_test)
add_custom_command(TARGET broken_bond_runtime_input_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:rex::runtime> $<TARGET_FILE_DIR:broken_bond_runtime_input_test>)
add_test(NAME launcher_runtime_input COMMAND broken_bond_runtime_input_test)
