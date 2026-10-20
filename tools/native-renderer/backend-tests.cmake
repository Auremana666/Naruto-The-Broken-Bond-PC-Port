# Host tests reused from Rise b3c03de; no guest execution.
# The engine with both backends for the host tests (no guest code), with
# the test hooks and the embedded shader packages (the Vulkan backend
# checks the SPIR-V contract): the common tests run on the API of
# REX_NATIVE_TEST_API (backend/tests/test_backend.h).
add_library(native_engine_test_units STATIC src/gpu/backend/engine.cpp
            ${BROKENBOND_NATIVE_VULKAN_SOURCES} src/gpu/backend/failure_channel.cpp
            src/gpu/backend/pipeline_keys.cpp src/gpu/backend/pipeline_set.cpp src/gpu/backend/dispatcher.cpp
            src/gpu/shaders/package.cpp src/gpu/shaders/shader_cache_data.cpp)
target_include_directories(native_engine_test_units PUBLIC src)
target_link_libraries(native_engine_test_units PUBLIC native_renderer_plume_vulkan
                      broken_bond_smolv fmt::fmt)
if(BROKENBOND_NATIVE_D3D12)
    target_sources(native_engine_test_units PRIVATE src/gpu/backend/d3d12/backend.cpp)
    target_link_libraries(native_engine_test_units PUBLIC native_renderer_plume)
else()
    target_compile_definitions(native_engine_test_units PUBLIC BROKENBOND_NATIVE_D3D12_DISABLED=1)
endif()
target_compile_definitions(native_engine_test_units PUBLIC BROKENBOND_NATIVE_VULKAN=1
                           NATIVE_RENDERER_ENGINE_FAULT_TEST=1 BROKENBOND_SHADER_CACHE_SPIRV=1
                           _CRT_SECURE_NO_WARNINGS)
add_dependencies(native_engine_test_units broken_bond_native_shader_cache)
foreach(_broken_bond_engine_test IN ITEMS latch lifetime failure translation resource depth_resolve depth_only)
    add_executable(native_engine_${_broken_bond_engine_test}_test
                   src/gpu/backend/tests/${_broken_bond_engine_test}_test.cpp)
    target_link_libraries(native_engine_${_broken_bond_engine_test}_test PRIVATE native_engine_test_units)
endforeach()
# The Vulkan tests: the completion service (held submissions, injected
# failures and the race) and the check of the SPIR-V contract.
foreach(_broken_bond_engine_test IN ITEMS completion shader_contract present draw_cache instancing)
    add_executable(native_engine_${_broken_bond_engine_test}_test
                   src/gpu/backend/vulkan/tests/${_broken_bond_engine_test}_test.cpp)
    target_link_libraries(native_engine_${_broken_bond_engine_test}_test PRIVATE native_engine_test_units)
endforeach()
set(_draw_cache_shaders "${CMAKE_CURRENT_BINARY_DIR}/draw-cache-test")
get_filename_component(_draw_cache_thirdparty "${BROKENBOND_XENOS_RECOMP_THIRDPARTY}" ABSOLUTE)
file(MAKE_DIRECTORY "${_draw_cache_shaders}")
foreach(_stage IN ITEMS vs ps)
    if(_stage STREQUAL "vs")
        set(_invert -fvk-invert-y)
        set(_symbol kCacheVs)
    else()
        set(_invert)
        set(_symbol kCachePs)
    endif()
    add_custom_command(OUTPUT "${_draw_cache_shaders}/draw_cache_${_stage}.h"
        COMMAND "${_draw_cache_thirdparty}/dxc-bin/bin/x64/dxc.exe"
            -T ${_stage}_6_0 -E ${_stage}_main -HV 2021 -spirv -fspv-target-env=vulkan1.2
            -fvk-use-dx-layout -fvk-stage-io-order=decl -fspv-entrypoint-name=main ${_invert}
            -Vn ${_symbol} -Fh "${_draw_cache_shaders}/draw_cache_${_stage}.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/backend/vulkan/tests/draw_cache.hlsl"
        DEPENDS src/gpu/backend/vulkan/tests/draw_cache.hlsl
                "${_draw_cache_thirdparty}/dxc-bin/bin/x64/dxc.exe" VERBATIM)
    target_sources(native_engine_draw_cache_test PRIVATE "${_draw_cache_shaders}/draw_cache_${_stage}.h")
endforeach()
target_include_directories(native_engine_draw_cache_test PRIVATE "${_draw_cache_shaders}")
foreach(_stage IN ITEMS vs ps)
    if(_stage STREQUAL "vs")
        set(_invert -fvk-invert-y)
        set(_symbol kInstanceVs)
    else()
        set(_invert)
        set(_symbol kInstancePs)
    endif()
    add_custom_command(OUTPUT "${_draw_cache_shaders}/instancing_${_stage}.h"
        COMMAND "${_draw_cache_thirdparty}/dxc-bin/bin/x64/dxc.exe"
            -T ${_stage}_6_0 -E ${_stage}_main -HV 2021 -spirv -fspv-target-env=vulkan1.2
            -fvk-use-dx-layout -fvk-stage-io-order=decl -fspv-entrypoint-name=main ${_invert}
            -Vn ${_symbol} -Fh "${_draw_cache_shaders}/instancing_${_stage}.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/backend/vulkan/tests/instancing.hlsl"
        DEPENDS src/gpu/backend/vulkan/tests/instancing.hlsl
                "${_draw_cache_thirdparty}/dxc-bin/bin/x64/dxc.exe" VERBATIM)
    target_sources(native_engine_instancing_test PRIVATE "${_draw_cache_shaders}/instancing_${_stage}.h")
endforeach()
target_include_directories(native_engine_instancing_test PRIVATE "${_draw_cache_shaders}")
# Host test of the Vulkan device (no guest code): clear, upload, copy and
# readback, with a bounded wait on the submission.
add_executable(native_vulkan_transfer_test src/gpu/backend/vulkan/tests/transfer_test.cpp)
target_include_directories(native_vulkan_transfer_test PRIVATE src)
target_link_libraries(native_vulkan_transfer_test PRIVATE native_renderer_plume_vulkan)
target_compile_definitions(native_vulkan_transfer_test PRIVATE _CRT_SECURE_NO_WARNINGS)
# Host test of the embedded shader packages (no guest code, no GPU): every
# SPIR-V module decodes to the SHA-256 of its index line.
add_executable(native_shader_package_test src/gpu/shaders/tests/package_test.cpp
               src/gpu/shaders/package.cpp src/gpu/shaders/shader_cache_data.cpp)
target_include_directories(native_shader_package_test PRIVATE src)
target_link_libraries(native_shader_package_test PRIVATE broken_bond_smolv fmt::fmt)
target_compile_definitions(native_shader_package_test PRIVATE BROKENBOND_SHADER_CACHE_SPIRV=1 NOMINMAX
                           WIN32_LEAN_AND_MEAN)
add_dependencies(native_shader_package_test broken_bond_native_shader_cache)
set(native_tests latch lifetime failure translation resource depth_resolve depth_only completion shader_contract present draw_cache instancing)
set(native_test_targets native_vulkan_transfer_test native_shader_package_test)
foreach(case IN LISTS native_tests)
    list(APPEND native_test_targets native_engine_${case}_test)
endforeach()
add_custom_target(broken_bond_native_tests DEPENDS ${native_test_targets})
foreach(target IN LISTS native_test_targets)
    if(target STREQUAL "native_shader_package_test")
        add_test(NAME ${target} COMMAND ${target})
    else()
        add_test(NAME ${target} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/run-backend-test.py" "$<TARGET_FILE:${target}>"
            "${CMAKE_CURRENT_BINARY_DIR}/backend-test-results")
    endif()
    set_tests_properties(${target} PROPERTIES TIMEOUT 90 ENVIRONMENT "REX_NATIVE_TEST_API=vulkan" RUN_SERIAL TRUE)
endforeach()
