#pragma once

// The internal resolution scale, in one place. The title keeps its own
// sizes (1280 x 720 and the other targets of sub_8215A660); the engine creates
// its surfaces and resolve destinations k times larger and maps here what comes
// in guest pixels: sizes, rectangle edges (scissor, clears, resolves) and the
// viewport. Each edge is mapped alone, round(edge * k), so rectangles that touch
// in guest pixels still touch at a fractional k (1.5x). A guest pixel stays
// whole, as in the SDK: the half-pixel offset is in guest pixels (NDC of the
// guest viewport) and ps_param_gen gives floor(host position) / k.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gpu {
struct ResolutionScale {
  float x = 1.0f, y = 1.0f;
  bool Unit() const { return x == 1.0f && y == 1.0f; }
  int32_t X(int32_t edge) const { return Map(edge, x); }
  int32_t Y(int32_t edge) const { return Map(edge, y); }
  uint32_t Width(uint32_t guest) const { return uint32_t(Map(int32_t(guest), x)); }
  uint32_t Height(uint32_t guest) const { return uint32_t(Map(int32_t(guest), y)); }
 private:
  static int32_t Map(int32_t edge, float k) { return k == 1.0f ? edge : int32_t(std::lround(double(edge) * k)); }
};

// The image of a scaled resolve that the CPU reads (WriteBackResolve), reduced
// to the guest size by the area average: a 2 x 2 box at 2x, fractional weights
// at 1.5x. in holds in_width x in_height RGBA8 texels.
inline std::vector<uint8_t> DownscaleRgba(const std::vector<uint8_t>& in, uint32_t in_width, uint32_t in_height,
                                          uint32_t width, uint32_t height) {
  struct Tap {
    uint32_t at;
    double weight;
  };
  const auto taps = [](uint32_t in_size, uint32_t size) {
    std::vector<std::vector<Tap>> out(size);
    const double step = double(in_size) / size;
    for (uint32_t o = 0; o < size; ++o) {
      const double begin = o * step, end = begin + step;
      for (uint32_t i = uint32_t(begin); i < in_size && i < end; ++i) {
        const double covered = std::min(end, i + 1.0) - std::max(begin, double(i));
        if (covered > 0) out[o].push_back({i, covered / step});
      }
    }
    return out;
  };
  const auto columns = taps(in_width, width), rows = taps(in_height, height);
  std::vector<uint8_t> out(size_t(width) * height * 4);
  for (uint32_t y = 0; y < height; ++y)
    for (uint32_t x = 0; x < width; ++x) {
      double sum[4] = {};
      for (const Tap& row : rows[y])
        for (const Tap& column : columns[x]) {
          const uint8_t* texel = &in[(size_t(row.at) * in_width + column.at) * 4];
          for (unsigned c = 0; c < 4; ++c) sum[c] += texel[c] * row.weight * column.weight;
        }
      for (unsigned c = 0; c < 4; ++c) out[(size_t(y) * width + x) * 4 + c] = uint8_t(std::min(255.0, sum[c] + 0.5));
    }
  return out;
}
}  // namespace gpu
