// Encodes the SPIR-V modules of the shader cache with smol-v, for the package
// of the Vulkan backend (build-shader-cache.py). Lossless: no encode flags, and
// each module is decoded back and compared, so the renderer decodes the bytes
// spirv-val checked. A host tool of the build, with the smol-v of the
// XenosRecomp dependencies (the fork encodes its SPIR-V cache with it too).
//
// Usage: smolv-encode <list>, each line "<module .spv>\t<output>". Exits 1
// naming the first module that does not read, encode, decode the same or write.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "smolv.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: smolv-encode <list>\n");
    return 2;
  }
  std::ifstream list(argv[1]);
  size_t modules = 0, raw = 0, encoded = 0;
  for (std::string line; std::getline(list, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    const std::string input = line.substr(0, tab), output = line.substr(tab + 1);
    std::ifstream in(input, std::ios::binary);
    const std::vector<char> spirv((std::istreambuf_iterator<char>(in)), {});
    smolv::ByteArray smol;
    if (spirv.empty() || !smolv::Encode(spirv.data(), spirv.size(), smol, 0)) {
      std::fprintf(stderr, "smolv-encode: cannot encode %s\n", input.c_str());
      return 1;
    }
    std::vector<char> decoded(smolv::GetDecodedBufferSize(smol.data(), smol.size()));
    if (!smolv::Decode(smol.data(), smol.size(), decoded.data(), decoded.size()) || decoded != spirv) {
      std::fprintf(stderr, "smolv-encode: %s does not decode to the same bytes\n", input.c_str());
      return 1;
    }
    std::ofstream out(output, std::ios::binary);
    out.write(reinterpret_cast<const char*>(smol.data()), std::streamsize(smol.size()));
    if (!out) {
      std::fprintf(stderr, "smolv-encode: cannot write %s\n", output.c_str());
      return 1;
    }
    ++modules;
    raw += spirv.size();
    encoded += smol.size();
  }
  std::printf("smolv-encode: %zu modules, %zu bytes, %zu encoded\n", modules, raw, encoded);
  return 0;
}
