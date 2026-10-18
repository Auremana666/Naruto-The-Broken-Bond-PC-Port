// The shader cache embedded in the build. The build generates the package
// from the local game files (tools/native-renderer/
// shader-cache.cmake) into its own directory, the embed directory of this unit
// (--embed-dir; identity in shader_cache.json next to it); the draw layer decompresses
// and reads it (LoadShaderCache). #embed is a clang extension in C++; clang 19
// emits the bytes as char.
#include <cstddef>

namespace gpu {
alignas(16) extern const char kShaderCache[] = {
#embed <shader_cache.bin>
};
extern const size_t kShaderCacheSize = sizeof(kShaderCache);
}  // namespace gpu
