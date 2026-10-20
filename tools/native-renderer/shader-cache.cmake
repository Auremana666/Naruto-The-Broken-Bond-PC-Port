# The shader cache of the native renderer, generated in the build from the local game
# files. The XenosRecomp fork of the
# repository (tools/xenosrecomp) is built as a host tool, and build-shader-cache.py runs
# the chain inside the build directory: the containers of game/xeshader.sdb, the
# translation and DXC of each shader, the lib_6_3 libraries of the spec constants and
# the variants linked from them with the values the draw asks for, the index
# (identification only by hash) and the package that the given unit embeds
# (#embed <shader_cache.bin>, by the embed directory set here).
#
#   brokenbond_shader_cache(<target> <embedding source> [SPIRV PASSES <backend folder>])
#
# SPIRV (a build with the Vulkan backend): the chain also compiles the SPIR-V of each
# shader, checks it with BROKENBOND_SPIRV_VAL (build-sdb-shader-spirv.py), does the same
# for the passes of the backend folder (build-pass-spirv.py: its *.hlsl, fxaa and the
# SDK's modules in sdk_shaders/vulkan_spirv) and packs them, encoded by smol-v
# (<target>_smolv_encode, a host tool), into shader_cache_spirv.bin with the manifest;
# the source embeds it too (BROKENBOND_SHADER_CACHE_SPIRV) and the target links the
# smol-v decoder (<target>_smolv).
#
# BROKENBOND_GAME_DIR: the local game files (xeshader.sdb).
# BROKENBOND_XENOS_RECOMP_THIRDPARTY: the dependencies of XenosRecomp (fmt, xxHash, zstd,
# smol-v, dxc-bin), the submodules of the upstream checkout (tools/xenosrecomp/thirdparty).
# BROKENBOND_SHADER_STORE: the content-addressed store of the translated and compiled
# shaders, kept across builds (shader_store.py; empty: none). It derives from the game
# files, so it stays in a git-ignored folder; deleting it only makes the next build slower.
# BROKENBOND_SPIRV_VAL: spirv-val of SPIRV-Tools, for SPIRV (the Vulkan SDK's, or the one
# supplied through BROKENBOND_SPIRV_VAL).
include_guard(GLOBAL)
include(ExternalProject)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(brokenbond_shader_cache target source)
  cmake_parse_arguments(PARSE_ARGV 2 arg "SPIRV" "PASSES" "")
  set(tools "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
  get_filename_component(fork "${tools}/../xenosrecomp" ABSOLUTE)
  set(BROKENBOND_GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/game" CACHE PATH "Local game files (xeshader.sdb)")
  set(BROKENBOND_XENOS_RECOMP_THIRDPARTY "${fork}/thirdparty"
      CACHE PATH "thirdparty directory with the XenosRecomp dependencies")
  set(BROKENBOND_SHADER_STORE "${CMAKE_CURRENT_SOURCE_DIR}/build/shader-store"
      CACHE PATH "Store of the translated and compiled shaders kept across builds (empty: none)")
  get_filename_component(game "${BROKENBOND_GAME_DIR}" ABSOLUTE)
  get_filename_component(thirdparty "${BROKENBOND_XENOS_RECOMP_THIRDPARTY}" ABSOLUTE)
  foreach(input "${game}/xeshader.sdb" "${thirdparty}/dxc-bin/bin/x64/dxc.exe")
    if(NOT EXISTS "${input}")
      message(FATAL_ERROR "The shader cache of the native renderer needs ${input}")
    endif()
  endforeach()
  set(out "${CMAKE_CURRENT_BINARY_DIR}/shader_cache")
  set(spirv)
  set(spirv_outputs)
  set(spirv_encoder)
  set(pass_sources)
  if(arg_SPIRV)
    if(NOT arg_PASSES)
      message(FATAL_ERROR "brokenbond_shader_cache: SPIRV needs PASSES <backend folder>")
    endif()
    get_filename_component(passes "${arg_PASSES}" ABSOLUTE)
    file(GLOB pass_sources CONFIGURE_DEPENDS "${passes}/*.hlsl" "${passes}/fxaa/*"
         "${passes}/sdk_shaders/vulkan_spirv/*.h")
    find_program(BROKENBOND_SPIRV_VAL spirv-val
        HINTS "$ENV{VULKAN_SDK}/Bin"
        DOC "spirv-val of SPIRV-Tools, for the SPIR-V of the shader cache")
    if(NOT BROKENBOND_SPIRV_VAL)
      message(FATAL_ERROR "The SPIR-V of the shader cache needs spirv-val (Vulkan SDK or "
                          "a local SPIRV-Tools build): set BROKENBOND_SPIRV_VAL")
    endif()
    # smol-v (the XenosRecomp dependency, <target>_smolv): the build encodes the modules,
    # the renderer decodes them.
    add_library(${target}_smolv STATIC "${thirdparty}/smol-v/source/smolv.cpp")
    target_include_directories(${target}_smolv PUBLIC "${thirdparty}/smol-v/source")
    add_executable(${target}_smolv_encode "${tools}/smolv-encode.cpp")
    target_link_libraries(${target}_smolv_encode PRIVATE ${target}_smolv)
    target_link_libraries(${target} PRIVATE ${target}_smolv)
    target_compile_definitions(${target} PRIVATE BROKENBOND_SHADER_CACHE_SPIRV=1)
    set(spirv --spirv-val "${BROKENBOND_SPIRV_VAL}" --smolv-encode "$<TARGET_FILE:${target}_smolv_encode>"
              --passes "${passes}")
    set(spirv_outputs "${out}/shader_cache_spirv.bin" "${out}/shader_manifest.json")
    set(spirv_encoder ${target}_smolv_encode)  # Reruns the chain when the encoder changes.
  endif()

  # The fork as a host tool (the C compiler is the clang of the build, as C).
  # BUILD_ALWAYS runs its incremental build every time: without it an edit of
  # the fork's sources changes the store key of the translations while the old
  # executable still translates them.
  string(REGEX REPLACE "clang\\+\\+" "clang" c_compiler "${CMAKE_CXX_COMPILER}")
  set(recomp_build "${CMAKE_CURRENT_BINARY_DIR}/xenosrecomp")
  set(recomp "${recomp_build}/XenosRecomp/XenosRecomp${CMAKE_EXECUTABLE_SUFFIX}")
  ExternalProject_Add(${target}_xenosrecomp
      SOURCE_DIR "${fork}"
      BINARY_DIR "${recomp_build}"
      CMAKE_ARGS -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_C_COMPILER=${c_compiler}"
                 "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                 -DXENOS_RECOMP_DXIL=ON "-DXENOS_RECOMP_THIRDPARTY_ROOT=${thirdparty}"
      BUILD_COMMAND "${CMAKE_COMMAND}" --build "${recomp_build}" --target XenosRecomp
      BUILD_ALWAYS ON
      INSTALL_COMMAND ""
      BUILD_BYPRODUCTS "${recomp}")

  set(scripts build-shader-cache.py extract-sdb-containers.py build-sdb-shader-cache.py
      build-sdb-shader-libraries.py build-sdb-shader-variants.py build-native-shader-index.py
      build-embedded-shader-cache.py shader_store.py build-sdb-shader-spirv.py spirv_module.py
      build-shader-manifest.py build-pass-spirv.py)
  list(TRANSFORM scripts PREPEND "${tools}/")
  add_custom_command(
      OUTPUT "${out}/shader_cache.bin" "${out}/shader_cache.json" ${spirv_outputs}
      COMMAND "${Python3_EXECUTABLE}" -I "${tools}/build-shader-cache.py" "${game}/xeshader.sdb" "${recomp}"
              "${fork}/XenosRecomp/shader_common.h" "${thirdparty}/dxc-bin/bin/x64/dxc.exe" "${out}"
              "${BROKENBOND_SHADER_STORE}" ${spirv}
      DEPENDS "${game}/xeshader.sdb" "${recomp}" ${target}_xenosrecomp "${fork}/XenosRecomp/shader_common.h"
              ${scripts} ${spirv_encoder} ${pass_sources}
      COMMENT "Shader cache of the native renderer from ${game}/xeshader.sdb (XenosRecomp fork, DXC)"
      USES_TERMINAL
      VERBATIM)
    add_custom_target(${target}_native_shader_cache DEPENDS "${out}/shader_cache.bin")
  add_dependencies(${target} ${target}_native_shader_cache)
  # #embed <...> searches the embed directories of clang (--embed-dir), not -I.
  set_source_files_properties("${source}" PROPERTIES
      COMPILE_OPTIONS "-Wno-c23-extensions;--embed-dir=${out}"
      OBJECT_DEPENDS "${out}/shader_cache.bin;${spirv_outputs}")
endfunction()
