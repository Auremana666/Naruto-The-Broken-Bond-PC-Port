// Guest texture layout (layout.h).
#include "gpu/textures/layout.h"
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>

namespace gpu {
bool GetTextureLayout(const std::array<uint32_t, 6>& words, GuestTextureLayout& out, std::string& reason) {
  namespace xenos = rex::graphics::xenos;
  namespace texture_util = rex::graphics::texture_util;
  xenos::xe_gpu_texture_fetch_t fetch{};
  fetch.dword_0 = words[0];
  fetch.dword_1 = words[1];
  fetch.dword_2 = words[2];
  fetch.dword_3 = words[3];
  fetch.dword_4 = words[4];
  fetch.dword_5 = words[5];
  const xenos::DataDimension dimension = fetch.dimension;
  if (dimension == xenos::DataDimension::k3D ||
      (dimension == xenos::DataDimension::k2DOrStacked && fetch.stacked)) {
    reason = "texture-dimension-uncovered";
    return false;
  }
  uint32_t width_minus_1, height_minus_1, depth_minus_1, base_page, mip_page, mip_min, mip_max;
  texture_util::GetSubresourcesFromFetchConstant(fetch, &width_minus_1, &height_minus_1, &depth_minus_1, &base_page,
                                                 &mip_page, &mip_min, &mip_max);
  const xenos::TextureFormat format = fetch.format;
  const uint32_t width = width_minus_1 + 1, height = height_minus_1 + 1, depth = depth_minus_1 + 1;
  const texture_util::TextureGuestLayout layout = texture_util::GetGuestTextureLayout(
      dimension, fetch.pitch, width, height, depth, fetch.tiled, format, fetch.packed_mips, base_page != 0, mip_max);
  out.width = width;
  out.height = height;
  out.faces = dimension == xenos::DataDimension::kCube ? depth : 1;
  out.mip_min = mip_min;
  out.mip_max = mip_max;
  out.has_base = base_page != 0;
  out.levels.assign(mip_max + 1, LevelPlace{});
  for (uint32_t level = 0; level <= mip_max; ++level) {
    LevelPlace& place = out.levels[level];
    // The level's stored place: the base, a mip, or the packed tail (stored as
    // the base when the base is packed, else at mips[packed_level]).
    const bool packed = level >= layout.packed_level;
    const texture_util::TextureGuestLayout::Level* stored;
    if (level == 0) {
      stored = &layout.base;
      place.address = base_page << 12;
    } else {
      const uint32_t stored_level = packed ? layout.packed_level : level;
      stored = &layout.mips[stored_level];
      place.address = (mip_page << 12) + layout.mip_offsets_bytes[stored_level];
    }
    if (packed) {
      uint32_t x_blocks, y_blocks, z_blocks;
      texture_util::GetPackedMipOffset(width, height, depth, format, level, x_blocks, y_blocks, z_blocks);
      place.x0_blocks = x_blocks;
      place.y0_blocks = y_blocks;
    }
    place.row_pitch_bytes = stored->row_pitch_bytes;
    place.region_bytes = stored->array_slice_data_extent_bytes;
    place.face_stride = stored->array_slice_stride_bytes;
  }
  return true;
}
}  // namespace gpu
