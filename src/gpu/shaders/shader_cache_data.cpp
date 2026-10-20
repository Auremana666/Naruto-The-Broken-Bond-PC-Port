// The shader cache embedded in the build. The build generates the packages
// from the local game files (tools/native-renderer/
// shader-cache.cmake) into its own directory, the embed directory of this unit
// (--embed-dir; identity in shader_cache.json next to it): the DXIL one, and the
// SPIR-V one in a build with the Vulkan backend (BROKENBOND_SHADER_CACHE_SPIRV;
// empty otherwise). The draw layer decompresses and reads the package of its API
// (package.h). #embed is a clang extension in C++; clang 19 emits the bytes as char.
#include <cstddef>

namespace gpu {
#if !defined(BROKENBOND_NATIVE_D3D12_DISABLED)
alignas(16) extern const char kShaderCache[] = {
#embed <shader_cache.bin>
};
extern const size_t kShaderCacheSize = sizeof(kShaderCache);
#else
extern const char kShaderCache[] = {0};
extern const size_t kShaderCacheSize = 0;
#endif
#if BROKENBOND_SHADER_CACHE_SPIRV
alignas(16) extern const char kShaderCacheSpirv[] = {
#embed <shader_cache_spirv.bin>
};
extern const size_t kShaderCacheSpirvSize = sizeof(kShaderCacheSpirv);
#else
extern const char kShaderCacheSpirv[] = {0};
extern const size_t kShaderCacheSpirvSize = 0;
#endif
}  // namespace gpu
