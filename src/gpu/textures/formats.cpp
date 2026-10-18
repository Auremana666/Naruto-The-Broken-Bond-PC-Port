// Guest texture formats of the renderer (formats.h).
#include "gpu/textures/formats.h"
#include <array>
#include <cstring>
#include <utility>

namespace gpu {
uint32_t TiledOffset2DOuter(uint32_t y, uint32_t pitch, uint32_t log2_bpp) {
  const uint32_t macro = ((y / 32) * (pitch / 32)) << (log2_bpp + 7);
  const uint32_t micro = ((y & 6) << 2) << log2_bpp;
  return macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 8) << (3 + log2_bpp)) + ((y & 1) << 4);
}
uint32_t TiledOffset2DInner(uint32_t x, uint32_t y, uint32_t log2_bpp, uint32_t base_offset) {
  const uint32_t macro = (x / 32) << (log2_bpp + 7);
  const uint32_t micro = (x & 7) << log2_bpp;
  const uint32_t offset = base_offset + (macro + ((micro & ~0xFu) << 1) + (micro & 0xF));
  return ((offset & ~0x1FFu) << 3) + ((offset & 0x1C0) << 2) + (offset & 0x3F) + ((y & 16) << 7) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}
uint32_t Log2Ceil(uint32_t v) { uint32_t n = 0; while ((1u << n) < v) ++n; return n; }

namespace {
// The rows of the SDK's host_formats_ covered here, by Xenos format number.
// The 13 formats Rise uses and Broken Bond's k_DXT5A (formats.h); the plain
// 8/32/64/128 bpb load shaders (the guest bytes after the endian swap),
// k_5_6_5, the depth conversions and the DXT decompressions of the SDK's load
// shaders. The depth formats are R32_FLOAT on the host.
using TC = TexelConversion;
struct Row {
  uint32_t format;
  GuestFormat host;
};
#define RGBA {0, 1, 2, 3}
#define RGBB {0, 1, 2, 2}
#define RGGG {0, 1, 1, 1}
#define RRRR {0, 0, 0, 0}
constexpr Row kRows[] = {
    {2, {DXGI_FORMAT_R8_UNORM, 1, 1, 1, RRRR, TC::kNone, false}},                         // k_8
    {4, {DXGI_FORMAT_B5G6R5_UNORM, 1, 2, 2, RGBB, TC::kR5G6B5ToB5G6R5, false}},           // k_5_6_5
    {6, {DXGI_FORMAT_R8G8B8A8_UNORM, 1, 4, 4, RGBA, TC::kNone, false}},                   // k_8_8_8_8
    {18, {DXGI_FORMAT_BC1_UNORM, 4, 8, 8, RGBA, TC::kNone, false}},                       // k_DXT1
    {20, {DXGI_FORMAT_BC3_UNORM, 4, 16, 16, RGBA, TC::kNone, false}},                     // k_DXT4_5
    {22, {DXGI_FORMAT_R32_FLOAT, 1, 4, 4, RRRR, TC::kDepthUnormToFloat, true}},           // k_24_8
    {23, {DXGI_FORMAT_R32_FLOAT, 1, 4, 4, RRRR, TC::kDepthFloatToFloat, true}},           // k_24_8_FLOAT
    {32, {DXGI_FORMAT_R16G16B16A16_FLOAT, 1, 8, 8, RGBA, TC::kNone, true}},               // k_16_16_16_16_FLOAT
    {36, {DXGI_FORMAT_R32_FLOAT, 1, 4, 4, RRRR, TC::kNone, true}},                        // k_32_FLOAT
    {37, {DXGI_FORMAT_R32G32_FLOAT, 1, 8, 8, RGGG, TC::kNone, true}},                     // k_32_32_FLOAT
    {38, {DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, 16, RGBA, TC::kNone, true}},             // k_32_32_32_32_FLOAT
    {49, {DXGI_FORMAT_BC5_UNORM, 4, 16, 16, RGGG, TC::kNone, false}},                     // k_DXN
    {59, {DXGI_FORMAT_BC4_UNORM, 4, 8, 8, RRRR, TC::kNone, false}},                       // k_DXT5A
    {60, {DXGI_FORMAT_R8G8_UNORM, 4, 8, 2, RGGG, TC::kCTX1ToRG8, false}},                 // k_CTX1
};
// The decompressed host formats of the block-compressed ones (the SDK's
// dxgi_format_uncompressed and its decompressing load shader).
constexpr Row kDecompressedRows[] = {
    {18, {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 8, 4, RGBA, TC::kDXT1ToRGBA8, false}},   // k_DXT1
    {20, {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 16, 4, RGBA, TC::kDXT5ToRGBA8, false}},  // k_DXT4_5
    {49, {DXGI_FORMAT_R8G8_UNORM, 4, 16, 2, RGGG, TC::kDXNToRG8, false}},         // k_DXN
    {59, {DXGI_FORMAT_R8_UNORM, 4, 8, 1, RRRR, TC::kDXT5AToR8, false}},           // k_DXT5A
};
#undef RGBA
#undef RGBB
#undef RGGG
#undef RRRR
template <size_t N>
constexpr std::array<int8_t, 64> MakeIndex(const Row (&rows)[N]) {
  std::array<int8_t, 64> index{};
  for (auto& i : index) i = -1;
  for (size_t i = 0; i < N; ++i) index[rows[i].format] = int8_t(i);
  return index;
}
constexpr std::array<int8_t, 64> kIndex = MakeIndex(kRows);
constexpr std::array<int8_t, 64> kDecompressedIndex = MakeIndex(kDecompressedRows);
}  // namespace

const GuestFormat* FindGuestFormat(uint32_t format, bool decompressed) {
  if (format >= 64) return nullptr;
  if (decompressed) return kDecompressedIndex[format] >= 0 ? &kDecompressedRows[kDecompressedIndex[format]].host : nullptr;
  return kIndex[format] >= 0 ? &kRows[kIndex[format]].host : nullptr;
}

bool DecompressesBlocks(TexelConversion conversion) {
  return conversion >= TexelConversion::kDXT1ToRGBA8 && conversion <= TexelConversion::kCTX1ToRG8;
}

bool ReadGuestLevel(const GuestLevel& level, std::vector<uint8_t>& out) {
  const uint32_t bpb = level.bytes_per_block, log2_bpp = Log2Ceil(bpb);
  // The endian swap of the SDK's load shaders goes by the element size
  // (native_texture_formats_test and the DXBC of texture_load_*_cs): the 8 bpb
  // shader never swaps (it does not read the
  // endian field), the 16 bpb ones (and r5g6b5_b5g6r5) swap the bytes of each
  // 16-bit element in 8in16 only, the 32 bpb and larger ones swap each 32-bit
  // word in every mode (8in16, 8in32, 16in32).
  const uint32_t mode = bpb == 1 ? 0 : bpb == 2 ? (level.endian == 1 ? 1 : 0) : level.endian;
  out.resize(size_t(level.width_blocks) * level.height_blocks * bpb);
  uint8_t* to = out.data();
  for (uint32_t y = 0; y < level.height_blocks; ++y) {
    const uint32_t row = level.y0_blocks + y;
    const uint32_t outer = TiledOffset2DOuter(row, level.pitch_blocks, log2_bpp);
    for (uint32_t x = 0; x < level.width_blocks; ++x, to += bpb) {
      const uint32_t column = level.x0_blocks + x;
      const uint32_t at = level.tiled ? (TiledOffset2DInner(column, row, log2_bpp, outer) >> log2_bpp) << log2_bpp
                                      : row * level.row_pitch_bytes + column * bpb;
      if (at + bpb > level.region_bytes) return false;
      std::memcpy(to, level.memory + at, bpb);
      if (mode == 1)
        for (uint32_t i = 0; i < bpb; i += 2) std::swap(to[i], to[i + 1]);
      else if (mode == 2)
        for (uint32_t i = 0; i < bpb; i += 4) std::swap(to[i], to[i + 3]), std::swap(to[i + 1], to[i + 2]);
      else if (mode == 3)
        for (uint32_t i = 0; i < bpb; i += 4) std::swap(to[i], to[i + 2]), std::swap(to[i + 1], to[i + 3]);
    }
  }
  return true;
}

namespace {
// The DXT decoders of the SDK's load shaders (texture_load_dxt*_cs): the 565
// endpoints widened to 888 by bit replication, the interpolated colors and
// alphas with unsigned (floor) divisions, as their DXBC (udiv by 3, 5 and 7).
uint32_t Load32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint32_t Load16(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8); }
void Endpoint565(uint32_t c, uint32_t rgb[3]) {
  const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  rgb[0] = (r << 3) | (r >> 2);
  rgb[1] = (g << 2) | (g >> 4);
  rgb[2] = (b << 3) | (b >> 2);
}
// The 16 RGBA8 texels of a DXT color block; dxt1: the three-color mode when c0 <= c1.
void ColorBlock(const uint8_t* block, bool dxt1, uint8_t out[16][4]) {
  const uint32_t c0 = Load16(block), c1 = Load16(block + 2), indices = Load32(block + 4);
  uint32_t e0[3], e1[3], palette[4][4];
  Endpoint565(c0, e0);
  Endpoint565(c1, e1);
  const bool three = dxt1 && c0 <= c1;
  for (int k = 0; k < 3; ++k) {
    palette[0][k] = e0[k];
    palette[1][k] = e1[k];
    palette[2][k] = three ? (e0[k] + e1[k]) >> 1 : (2 * e0[k] + e1[k]) / 3;
    palette[3][k] = three ? 0 : (e0[k] + 2 * e1[k]) / 3;
  }
  palette[0][3] = palette[1][3] = palette[2][3] = 255;
  palette[3][3] = three ? 0 : 255;
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 4; ++k) out[i][k] = uint8_t(palette[(indices >> (2 * i)) & 3][k]);
}
// The 16 values of a DXT5 alpha block (also a DXN channel).
void AlphaBlock(const uint8_t* block, uint8_t out[16]) {
  const uint32_t a0 = block[0], a1 = block[1];
  uint64_t indices = 0;
  for (int i = 0; i < 6; ++i) indices |= uint64_t(block[2 + i]) << (8 * i);
  uint32_t palette[8] = {a0, a1};
  if (a0 > a1)
    for (uint32_t i = 2; i < 8; ++i) palette[i] = ((8 - i) * a0 + (i - 1) * a1) / 7;
  else {
    for (uint32_t i = 2; i < 6; ++i) palette[i] = ((6 - i) * a0 + (i - 1) * a1) / 5;
    palette[6] = 0;
    palette[7] = 255;
  }
  for (int i = 0; i < 16; ++i) out[i] = uint8_t(palette[(indices >> (3 * i)) & 7]);
}
// One 4 x 4 block into 16 host texels of texel_bytes each.
void DecompressBlock(TexelConversion conversion, const uint8_t* block, uint8_t* texels) {
  switch (conversion) {
    case TexelConversion::kDXT1ToRGBA8:
      ColorBlock(block, true, reinterpret_cast<uint8_t(*)[4]>(texels));
      break;
    case TexelConversion::kDXT5ToRGBA8: {
      ColorBlock(block + 8, false, reinterpret_cast<uint8_t(*)[4]>(texels));
      uint8_t alpha[16];
      AlphaBlock(block, alpha);
      for (int i = 0; i < 16; ++i) texels[4 * i + 3] = alpha[i];
      break;
    }
    case TexelConversion::kDXNToRG8: {
      uint8_t r[16], g[16];
      AlphaBlock(block, r);
      AlphaBlock(block + 8, g);
      for (int i = 0; i < 16; ++i) texels[2 * i] = r[i], texels[2 * i + 1] = g[i];
      break;
    }
    case TexelConversion::kDXT5AToR8:
      AlphaBlock(block, texels);
      break;
    case TexelConversion::kCTX1ToRG8: {
      // g0, r0, g1, r1, then the 2-bit indices (the SDK's ConvertTexelCTX1ToR8G8).
      const uint32_t g0 = block[0], r0 = block[1], g1 = block[2], r1 = block[3], indices = Load32(block + 4);
      const uint32_t r[4] = {r0, r1, (2 * r0 + r1) / 3, (r0 + 2 * r1) / 3};
      const uint32_t g[4] = {g0, g1, (2 * g0 + g1) / 3, (g0 + 2 * g1) / 3};
      for (int i = 0; i < 16; ++i) {
        const uint32_t k = (indices >> (2 * i)) & 3;
        texels[2 * i] = uint8_t(r[k]);
        texels[2 * i + 1] = uint8_t(g[k]);
      }
      break;
    }
    default:
      break;
  }
}
uint32_t TexelBytes(TexelConversion conversion) {
  if (conversion == TexelConversion::kDXT5AToR8) return 1;
  return conversion == TexelConversion::kDXT1ToRGBA8 || conversion == TexelConversion::kDXT5ToRGBA8 ? 4 : 2;
}
template <typename F>
void Map16(std::vector<uint8_t>& texels, F f) {
  for (size_t i = 0; i + 2 <= texels.size(); i += 2) {
    uint16_t v;
    std::memcpy(&v, &texels[i], 2);
    v = uint16_t(f(uint32_t(v)));
    std::memcpy(&texels[i], &v, 2);
  }
}
template <typename F>
void Map32(std::vector<uint8_t>& texels, F f) {
  for (size_t i = 0; i + 4 <= texels.size(); i += 4) {
    uint32_t v;
    std::memcpy(&v, &texels[i], 4);
    v = f(v);
    std::memcpy(&texels[i], &v, 4);
  }
}
}  // namespace

// The bit operations of the SDK's load shaders (the DXBC of texture_load_*_cs),
// per little-endian element after the endian swap.
void ConvertTexels(TexelConversion conversion, std::vector<uint8_t>& texels) {
  switch (conversion) {
    case TexelConversion::kR5G6B5ToB5G6R5:
      Map16(texels, [](uint32_t v) { return (v & 0x07E0) | ((v & 0x001F) << 11) | (v >> 11); });
      break;
    case TexelConversion::kDepthUnormToFloat:
      Map32(texels, [](uint32_t v) {
        const uint32_t d = v >> 8;
        const float f = float(d + (d >> 23)) * 0x1p-24f;
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return bits;
      });
      break;
    case TexelConversion::kDepthFloatToFloat:
      Map32(texels, [](uint32_t v) {
        const uint32_t d = v >> 8, mantissa = d & 0xFFFFF, exponent = d >> 20;
        if (!d) return 0u;
        // A denormal (exponent 0) is normalized: the shift that brings its top
        // bit to bit 20, the exponent 1 - shift (firstbit_hi of the DXBC).
        uint32_t shift = 21;
        if (mantissa) {
          uint32_t top = 19;
          while (!(mantissa >> top)) --top;
          shift = 20 - top;
        }
        const uint32_t e = exponent ? exponent : 1 - shift;
        const uint32_t m = exponent ? mantissa : (mantissa << shift) & 0xFFFFF;
        return ((e << 23) + 0x38000000u) + (m << 3);
      });
      break;
    default:
      break;
  }
}

void ConvertLevel(TexelConversion conversion, std::vector<uint8_t>& data, uint32_t width_blocks,
                  uint32_t height_blocks, uint32_t width, uint32_t height) {
  if (!DecompressesBlocks(conversion)) {
    ConvertTexels(conversion, data);
    return;
  }
  const uint32_t texel_bytes = TexelBytes(conversion);
  const size_t block_bytes = width_blocks && height_blocks ? data.size() / (size_t(width_blocks) * height_blocks) : 0;
  std::vector<uint8_t> texels(size_t(width) * height * texel_bytes);
  uint8_t block_texels[16 * 4];
  for (uint32_t by = 0; by < height_blocks; ++by)
    for (uint32_t bx = 0; bx < width_blocks; ++bx) {
      DecompressBlock(conversion, data.data() + (size_t(by) * width_blocks + bx) * block_bytes, block_texels);
      for (uint32_t y = 0; y < 4 && by * 4 + y < height; ++y)
        for (uint32_t x = 0; x < 4 && bx * 4 + x < width; ++x)
          std::memcpy(&texels[(size_t(by * 4 + y) * width + bx * 4 + x) * texel_bytes],
                      &block_texels[(y * 4 + x) * texel_bytes], texel_bytes);
    }
  data.swap(texels);
}
}  // namespace gpu
