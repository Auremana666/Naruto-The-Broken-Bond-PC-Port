cmake_minimum_required(VERSION 3.25)
project(broken_bond_setup LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The setup helper currently supports Windows only")
endif()

# Standalone build: no generated code, game data, SDK or SDL dependency.
add_executable(broken_bond_setup_helper
    ../launcher/src/setup_helper.cpp
    ../launcher/src/xiso_process.cpp
    ../launcher/src/game_files.cpp)
target_compile_features(broken_bond_setup_helper PRIVATE cxx_std_23)
target_link_libraries(broken_bond_setup_helper PRIVATE shell32 bcrypt)
set_target_properties(broken_bond_setup_helper PROPERTIES
    MSVC_RUNTIME_LIBRARY MultiThreaded)
if(MSVC)
    target_compile_options(broken_bond_setup_helper PRIVATE /utf-8)
endif()
