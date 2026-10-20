#pragma once

// Guest texture layout (layout.cpp): where each level and face of a
// texture lies in guest memory, from the SDK's own functions (texture_util of
// rexruntime: GetSubresourcesFromFetchConstant, GetGuestTextureLayout,
// GetPackedMipOffset), as its texture cache loads them
// (D3D12TextureCache::LoadTextureDataFromResidentMemoryImpl): the base at the
// base page, the mips at the mip page plus their offsets, a packed mip tail
// read once with each level at its block offset, the faces of a cube as array
// slices of each level.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gpu {
struct LevelPlace {
  uint32_t address = 0;                   // Guest physical address of the place.
  uint32_t row_pitch_bytes = 0;           // Bytes between rows of blocks (tiled: the pitch in blocks times the block).
  uint32_t x0_blocks = 0, y0_blocks = 0;  // The level's offset in a packed mip tail.
  uint32_t region_bytes = 0;              // Bytes of one face the level may read from address.
  uint32_t face_stride = 0;               // Bytes between the faces (array slices) of the place.
};
struct GuestTextureLayout {
  uint32_t width = 0, height = 0, faces = 1;
  uint32_t mip_min = 0, mip_max = 0;  // The SDK's clamped range (mip_max 0 without a mip page).
  bool has_base = false;
  std::vector<LevelPlace> levels;  // mip_max + 1 places.
};
// From a fetch constant (host word order, physical addresses). False for what
// has no host path here (3D and stacked textures), reason set.
bool GetTextureLayout(const std::array<uint32_t, 6>& fetch, GuestTextureLayout& out, std::string& reason);
}  // namespace gpu
