// The SPIR-V contract of the build against the device of the Vulkan backend.
// The embedded SPIR-V package carries the contract of its manifest in its
// index (tools/native-renderer/build-embedded-shader-cache.py): the
// capabilities and extensions the shaders and passes declare, and the
// bindings and push constant ranges of the draw layout and of each pass
// layout. Initialize checks them against what the device enabled and its
// limits, so a session never creates pipelines from shaders its device does
// not run.
#pragma once

#include <plume_vulkan.h>  // Vulkan through volk, with the platform defines of Plume.
#include <cstdint>
#include <string>
#include <vector>

namespace gpu::vulkan {

struct ShaderContract {
  std::string sha256;  // The manifest's contract_sha256.
  std::vector<std::string> capabilities, extensions;
  struct Binding {
    std::string layout;
    uint32_t set = 0, binding = 0;
    std::string type, detail;  // The detail is "-" when the type has none.
  };
  struct PushRange {
    std::string layout, stage;
    uint32_t begin = 0, end = 0;
  };
  std::vector<Binding> bindings;
  std::vector<PushRange> push_ranges;
};

// The contract of a SPIR-V package's index. False with error when a line is
// malformed or the index has no contract.
bool ReadShaderContract(const std::string& index, ShaderContract& contract, std::string& error);

// What the device lacks of the contract, each named ("capability:Int64", ...);
// a requirement the backend does not know is named as unmapped. Empty when the
// device has everything.
std::vector<std::string> MissingFromDevice(const ShaderContract& contract, const plume::VulkanDevice& device);

}  // namespace gpu::vulkan
