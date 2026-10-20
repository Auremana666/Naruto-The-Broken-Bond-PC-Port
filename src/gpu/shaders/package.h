#pragma once

// The shader packages embedded in the build (shader_cache_data.cpp), one per
// binary format, as tools/native-renderer/build-embedded-shader-cache.py
// writes them: a container compressed by MSZIP (the Windows Compression API)
// whose payload, checked by SHA-256, holds the index text and one record per
// file. The DXIL package has the DXIL of each shader and the spec constant
// variants linked in the build. The SPIR-V package (builds with the Vulkan
// backend) has the module of each shader and of each pass (presentation,
// resolves, FXAA and the SDK's output effects) encoded by smol-v, and the
// manifest of the build; its index carries the contract with its requirements
// and layouts, a line per pass and the SHA-256 of each decoded module. A session unpacks only the package of
// its API.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "gpu/backend/contracts.h"

namespace gpu {
struct ShaderPackage {
  ShaderFormat format = ShaderFormat::kDxil;
  std::vector<uint8_t> payload;  // Decompressed; the spans below point into it.
  std::string index;  // The index text (index format 4).
  // By shader name, the module as stored (DXIL, or SPIR-V encoded by smol-v);
  // the DXIL variants by "<name>+spec<value>"; SPIR-V: the passes by name.
  std::unordered_map<std::string, std::span<const uint8_t>> modules, variants, passes;
  std::string manifest;  // SPIR-V: the manifest of the build (JSON).
  size_t container_bytes = 0;
  std::string payload_sha256;
};

// Unpacks the embedded package of the format. False with error naming the
// failed check; "embedded-format-absent" when the build has no package of it.
bool UnpackShaderPackage(ShaderFormat format, ShaderPackage& package, std::string& error);
// The embedded package of the format, unpacked at the first call and kept for
// the process (the Vulkan backend's contract and passes); null with the error
// of that unpack when it failed.
const ShaderPackage* EmbeddedShaderPackage(ShaderFormat format, std::string& error);
// A module as the backend takes it: the DXIL as stored, the SPIR-V decoded
// from smol-v. Empty when it does not decode.
std::vector<uint8_t> DecodeShaderModule(const ShaderPackage& package, std::span<const uint8_t> stored);
}  // namespace gpu
