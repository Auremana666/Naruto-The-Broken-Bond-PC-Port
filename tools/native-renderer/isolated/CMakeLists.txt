cmake_minimum_required(VERSION 3.25)
project(broken_bond_native_backend LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The isolated D3D12 backend requires Windows x64")
endif()

get_filename_component(native_project_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(BROKENBOND_NATIVE_D3D12 ON)
set(BROKENBOND_NATIVE_VULKAN OFF)
include("${CMAKE_CURRENT_LIST_DIR}/../plume-engine.cmake")
set(NATIVE_RENDERER_FMT_SOURCE "" CACHE PATH "Clean fmt checkout at the accepted revision")
if(NOT NATIVE_RENDERER_FMT_SOURCE)
    message(FATAL_ERROR "Native backend requires NATIVE_RENDERER_FMT_SOURCE")
endif()
string(JSON fmt_revision GET "${NATIVE_RENDERER_DEPENDENCIES}" xenosrecomp_thirdparty fmt revision)
native_renderer_require_source(fmt "${NATIVE_RENDERER_FMT_SOURCE}" "${fmt_revision}")

# Use the pinned library sources under a private name: an SDK imported by the
# parent project may already define fmt::fmt at a different revision.
add_library(native_renderer_fmt STATIC
    "${NATIVE_RENDERER_FMT_SOURCE}/src/format.cc"
    "${NATIVE_RENDERER_FMT_SOURCE}/src/os.cc")
target_include_directories(native_renderer_fmt PUBLIC "${NATIVE_RENDERER_FMT_SOURCE}/include")
target_compile_features(native_renderer_fmt PUBLIC cxx_std_11)

# Object files go directly into the executable, so an empty main cannot leave
# the backend unexamined in a static archive. Compile the real sampler as well.
add_library(native_renderer_backend OBJECT
    "${native_project_root}/src/gpu/backend/engine.cpp"
    "${native_project_root}/src/gpu/backend/d3d12/backend.cpp"
    "${native_project_root}/src/gpu/backend/failure_channel.cpp"
    "${native_project_root}/src/gpu/backend/pipeline_keys.cpp"
    "${native_project_root}/src/gpu/backend/pipeline_set.cpp"
    "${native_project_root}/src/gpu/backend/dispatcher.cpp"
    "${native_project_root}/src/gpu/diagnostics/sampler.cpp")
target_include_directories(native_renderer_backend PUBLIC "${native_project_root}/src")
target_compile_features(native_renderer_backend PUBLIC cxx_std_23)
target_link_libraries(native_renderer_backend PUBLIC native_renderer_plume native_renderer_fmt psapi user32)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT MSVC)
    # sampler.cpp uses Windows SEH to bound native stack unwinding.
    target_compile_options(native_renderer_backend PRIVATE -fms-extensions)
endif()

add_executable(broken_bond_native_backend link_main.cpp)
target_link_libraries(broken_bond_native_backend PRIVATE native_renderer_backend)
# Keep uncalled backend methods in the link and write a symbol map for inspection.
# Cases cover readiness/shutdown, one clear/readback and its window presentation.
# No guest or graphics hook is linked.
target_link_options(broken_bond_native_backend PRIVATE
    "LINKER:/OPT:NOREF" "LINKER:/MAP:$<TARGET_FILE_DIR:broken_bond_native_backend>/native-backend.map")
