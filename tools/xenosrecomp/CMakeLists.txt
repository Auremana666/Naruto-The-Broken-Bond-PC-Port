cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 17)

project("XenosRecomp-ALL")

# The dependencies (fmt, xxHash, zstd, smol-v, dxc-bin) are the submodules of the
# upstream checkout (hedge-dev/XenosRecomp 990d03b), in thirdparty/ as upstream lays them out.
set(XENOS_RECOMP_THIRDPARTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
    CACHE PATH "thirdparty directory with the XenosRecomp dependencies")

add_subdirectory(${XENOS_RECOMP_THIRDPARTY_ROOT} thirdparty)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/XenosRecomp")
