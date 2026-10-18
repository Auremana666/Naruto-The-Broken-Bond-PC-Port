# The D3D12 backend of the native renderer: Plume at a pinned revision. A supplied
# checkout is mandatory (NATIVE_RENDERER_PLUME_SOURCE), clean at the pinned commits.
set(NATIVE_RENDERER_PLUME_SOURCE "" CACHE PATH "Clean Plume checkout at the accepted revision")
if(NOT NATIVE_RENDERER_PLUME_SOURCE)
    message(FATAL_ERROR "Native backend requires NATIVE_RENDERER_PLUME_SOURCE")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/pinned-source.cmake")
string(JSON plume_revision GET "${NATIVE_RENDERER_DEPENDENCIES}" plume revision)
string(JSON allocator_revision GET "${NATIVE_RENDERER_DEPENDENCIES}" plume allocator_revision)
native_renderer_require_source(plume "${NATIVE_RENDERER_PLUME_SOURCE}" "${plume_revision}")
native_renderer_require_source(allocator
    "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/D3D12MemoryAllocator" "${allocator_revision}")
# Compiled from the upstream sources, unchanged.
add_library(native_renderer_plume STATIC
    "${NATIVE_RENDERER_PLUME_SOURCE}/plume_d3d12.cpp"
    "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp")
target_include_directories(native_renderer_plume PUBLIC "${NATIVE_RENDERER_PLUME_SOURCE}"
    "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/D3D12MemoryAllocator/include")
target_compile_features(native_renderer_plume PUBLIC cxx_std_17)
target_compile_definitions(native_renderer_plume PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(native_renderer_plume PUBLIC d3d12 dxgi dxguid)
