# The shader cache of the native renderer, generated in the build from the local game
# files. The XenosRecomp fork of the
# repository (tools/xenosrecomp) is built as a host tool, and build-shader-cache.py runs
# the chain inside the build directory: the containers of game/xeshader.sdb, the
# translation and DXC of each shader, the lib_6_3 libraries of the spec constants and
# the variants linked from them with the values the draw asks for, the index
# (identification only by hash) and the package that the given unit embeds
# (#embed <shader_cache.bin>, by the embed directory set here).
#
#   brokenbond_shader_cache(<target> <embedding source>)
#
# BROKENBOND_GAME_DIR: the local game files (xeshader.sdb).
# BROKENBOND_XENOS_RECOMP_THIRDPARTY: the dependencies of XenosRecomp (fmt, xxHash, zstd,
# smol-v, dxc-bin), the submodules of the upstream checkout (tools/xenosrecomp/thirdparty).
# BROKENBOND_SHADER_STORE: the content-addressed store of the translated and compiled
# shaders, kept across builds (shader_store.py; empty: none). It derives from the game
# files, so it stays in a git-ignored folder; deleting it only makes the next build slower.
include_guard(GLOBAL)
include(ExternalProject)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(brokenbond_shader_cache target source)
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

  set(out "${CMAKE_CURRENT_BINARY_DIR}/shader_cache")
  set(scripts build-shader-cache.py extract-sdb-containers.py build-sdb-shader-cache.py
      build-sdb-shader-libraries.py build-sdb-shader-variants.py build-native-shader-index.py
      build-embedded-shader-cache.py shader_store.py)
  list(TRANSFORM scripts PREPEND "${tools}/")
  add_custom_command(
      OUTPUT "${out}/shader_cache.bin" "${out}/shader_cache.json"
      COMMAND "${Python3_EXECUTABLE}" -I "${tools}/build-shader-cache.py" "${game}/xeshader.sdb" "${recomp}"
              "${fork}/XenosRecomp/shader_common.h" "${thirdparty}/dxc-bin/bin/x64/dxc.exe" "${out}"
              "${BROKENBOND_SHADER_STORE}"
      DEPENDS "${game}/xeshader.sdb" "${recomp}" ${target}_xenosrecomp "${fork}/XenosRecomp/shader_common.h"
              ${scripts}
      COMMENT "Shader cache of the native renderer from ${game}/xeshader.sdb (XenosRecomp fork, DXC)"
      USES_TERMINAL
      VERBATIM)
    add_custom_target(${target}_native_shader_cache DEPENDS "${out}/shader_cache.bin")
  add_dependencies(${target} ${target}_native_shader_cache)
  # #embed <...> searches the embed directories of clang (--embed-dir), not -I.
  set_source_files_properties("${source}" PROPERTIES
      COMPILE_OPTIONS "-Wno-c23-extensions;--embed-dir=${out}"
      OBJECT_DEPENDS "${out}/shader_cache.bin")
endfunction()
