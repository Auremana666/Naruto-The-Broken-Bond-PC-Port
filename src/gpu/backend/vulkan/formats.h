// The Vulkan values of the engine's contracts (backend/contracts.h): texture
// formats and component mappings. Header-only, so the Vulkan host tests use the
// same table as the backend.
#pragma once

#include <plume_vulkan.h>  // Vulkan through volk, with the platform defines of Plume.
#include "gpu/backend/contracts.h"

namespace gpu::vulkan {

// The contract numbers are the DXGI formats (contracts.h), whose components
// are named from the lowest bits up for the packed formats: B5G6R5 has blue in
// bits 0..4, which is Vulkan's R5G6B5_UNORM_PACK16 (named from the highest bits).
inline VkFormat ImageFormat(TextureFormat format) {
  switch (format) {
    case TextureFormat::kR32G32B32A32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case TextureFormat::kR16G16B16A16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::kR32G32Float: return VK_FORMAT_R32G32_SFLOAT;
    case TextureFormat::kR8G8B8A8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::kR32Float: return VK_FORMAT_R32_SFLOAT;
    case TextureFormat::kR8G8Unorm: return VK_FORMAT_R8G8_UNORM;
    case TextureFormat::kR8Unorm: return VK_FORMAT_R8_UNORM;
    case TextureFormat::kBC1Unorm: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case TextureFormat::kBC4Unorm: return VK_FORMAT_BC4_UNORM_BLOCK;
    case TextureFormat::kBC3Unorm: return VK_FORMAT_BC3_UNORM_BLOCK;
    case TextureFormat::kBC5Unorm: return VK_FORMAT_BC5_UNORM_BLOCK;
    case TextureFormat::kB5G6R5Unorm: return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case TextureFormat::kUnknown: break;
  }
  return VK_FORMAT_UNDEFINED;
}

inline VkComponentSwizzle ComponentSwizzle(Swizzle swizzle) {
  switch (swizzle) {
    case Swizzle::kR: return VK_COMPONENT_SWIZZLE_R;
    case Swizzle::kG: return VK_COMPONENT_SWIZZLE_G;
    case Swizzle::kB: return VK_COMPONENT_SWIZZLE_B;
    case Swizzle::kA: return VK_COMPONENT_SWIZZLE_A;
    case Swizzle::kZero: return VK_COMPONENT_SWIZZLE_ZERO;
    case Swizzle::kOne: return VK_COMPONENT_SWIZZLE_ONE;
  }
  return VK_COMPONENT_SWIZZLE_IDENTITY;
}
inline VkComponentMapping ImageViewMapping(const ComponentMapping& mapping) {
  return {ComponentSwizzle(mapping[0]), ComponentSwizzle(mapping[1]), ComponentSwizzle(mapping[2]),
          ComponentSwizzle(mapping[3])};
}

}  // namespace gpu::vulkan
