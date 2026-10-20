#pragma once

// Guest texture formats of the renderer (formats.cpp): the host format of each
// Xenos texture format, as the SDK's D3D12 texture cache maps it (its
// host_formats_, sdk/src/graphics/d3d12/texture_cache.cpp), and the read of
// one level of guest memory into host texels (detiling, the endian swap of the
// guest words and the conversion of the SDK's load shader). Pure functions,
// without the rest of the renderer, so the host test can run them against the
// SDK's own load shaders (textures/tests/formats_test.cpp).
#include <cstdint>
#include <vector>
#include "gpu/backend/contracts.h"

namespace gpu {
// Xenos 2D tiling: 32x32-block macro tiles (crn_decomp.h offsets, as the SDK
// uses them). The byte offset of block (x, y) for a pitch in blocks.
uint32_t TiledOffset2DOuter(uint32_t y, uint32_t pitch, uint32_t log2_bpp);
uint32_t TiledOffset2DInner(uint32_t x, uint32_t y, uint32_t log2_bpp, uint32_t base_offset);
uint32_t Log2Ceil(uint32_t v);

// A conversion of the texels after the endian swap, as the load shaders of the
// SDK's D3D12 texture cache (the bit operations of their DXBC).
enum class TexelConversion : uint8_t {
  kNone,
  kR5G6B5ToB5G6R5,       // k_5_6_5: red and blue swapped (kLoadShaderIndexR5G6B5ToB5G6R5).
  kDepthUnormToFloat,    // k_24_8: (d + (d >> 23)) * 2^-24 of the 24 high bits.
  kDepthFloatToFloat,    // k_24_8_FLOAT: the 20e4 float of the 24 high bits as a 32-bit float.
  // Blocks of 4 x 4 texels decompressed into texels.
  kDXT1ToRGBA8,          // k_DXT1 when the base is not a multiple of 4.
  kDXT5ToRGBA8,          // k_DXT4_5, likewise.
  kDXNToRG8,             // k_DXN, likewise.
  kDXT5AToR8,            // k_DXT5A, likewise.
  kCTX1ToRG8,            // k_CTX1 (always: the SDK has no host CTX1).
};
// A conversion of 4 x 4 blocks into texels (host_bytes_per_block is then per texel).
bool DecompressesBlocks(TexelConversion conversion);

// The host side of a guest format (one row of the SDK's host_formats_).
struct GuestFormat {
  TextureFormat host;
  uint8_t block;                 // Block width and height in texels (4 for the compressed formats).
  uint8_t bytes_per_block;       // Guest bytes per block.
  uint8_t host_bytes_per_block;  // Host bytes per block after the conversion.
  uint8_t swizzle[4];            // The SDK's host swizzle: the host component each guest component reads.
  TexelConversion conversion;
  bool sign_free;                // The SDK's signed host format is the unsigned one (float and depth formats).
};
// The formats covered here; null: not covered (the draw is skipped and
// counted). The formats Rise uses, rows of the SDK's table: the five of
// its assets (k_DXT1, k_DXT4_5, k_8, k_DXN, k_CTX1, in game/naruto.bf) and the
// eight it creates at run time (k_8_8_8_8
// of the resolves, k_5_6_5 of the save thumbnail, k_24_8 and k_24_8_FLOAT of
// the depth resolves, k_16_16_16_16_FLOAT and the 32-bit floats of the vertex
// textures); Broken Bond adds k_DXT5A. decompressed: the SDK's host format when a block-compressed
// texture needs decompression (its base is not a multiple of 4 texels), the
// texels of the matching load shader.
const GuestFormat* FindGuestFormat(uint32_t format, bool decompressed = false);

// One level of guest memory (an address in the physical mapping) and its layout.
struct GuestLevel {
  const uint8_t* memory = nullptr;  // The place of the level (its address in the physical mapping).
  uint32_t region_bytes = 0;        // The bytes readable from memory.
  uint32_t pitch_blocks = 0;        // Tiled: the pitch of the place in blocks.
  uint32_t row_pitch_bytes = 0;     // Linear: the row pitch in bytes.
  bool tiled = false;
  uint32_t endian = 0;              // Xenos endian mode: 0 none, 1 8in16, 2 8in32, 3 16in32.
  uint32_t x0_blocks = 0, y0_blocks = 0;  // The offset of the level in its place (packed tail).
  uint32_t width_blocks = 0, height_blocks = 0;
  uint32_t bytes_per_block = 4;
};
// The level's blocks, row after row (width_blocks * bytes_per_block per row),
// each swapped by the endian mode as the SDK's load shaders swap it (by the
// element size: 8 bpb never, 16 bpb in 8in16 only, 32 bpb and larger in every
// mode). False: a block past region_bytes (the out vector is then incomplete).
bool ReadGuestLevel(const GuestLevel& level, std::vector<uint8_t>& out);
// The guest blocks of a level (after the endian swap) as host blocks.
void ConvertTexels(TexelConversion conversion, std::vector<uint8_t>& texels);
// A level of width_blocks x height_blocks guest blocks as its host data: the
// per-texel conversions of ConvertTexels, or the decompressed texels cropped
// to width x height (the SDK's load shaders write the rows below the height).
void ConvertLevel(TexelConversion conversion, std::vector<uint8_t>& data, uint32_t width_blocks,
                  uint32_t height_blocks, uint32_t width, uint32_t height);
}  // namespace gpu
