// The host rectangles of the engine's operations, the same for every backend:
// the guest edges of a request mapped by the resolution scale
// (common/resolution_scale.h) and clipped to the surface they act on.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "gpu/backend/contracts.h"

namespace gpu {

struct HostRect {
  int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// The rectangle a clear covers on a surface of width x height: the request's
// edges, or the whole surface when they are empty, clipped to the surface.
// False when nothing of the surface is left.
inline bool ClearRect(const ResolutionScale& k, const ClearRequest& request, uint32_t width, uint32_t height,
                      HostRect& rect) {
  rect = {k.X(request.left), k.Y(request.top), k.X(request.right), k.Y(request.bottom)};
  if (rect.right <= rect.left || rect.bottom <= rect.top) rect = {0, 0, int32_t(width), int32_t(height)};
  rect.left = std::max(rect.left, 0);
  rect.top = std::max(rect.top, 0);
  rect.right = std::min(rect.right, int32_t(width));
  rect.bottom = std::min(rect.bottom, int32_t(height));
  return rect.right > rect.left && rect.bottom > rect.top;
}

// A resolve in host pixels: the destination's size, the source rectangle and
// the destination point (the request's, in guest pixels).
struct HostResolve {
  uint32_t dest_width = 0, dest_height = 0, left = 0, top = 0, right = 0, bottom = 0, dest_x = 0, dest_y = 0;
};
inline HostResolve ResolveInHost(const ResolutionScale& k, const ResolveRequest& request) {
  return {k.Width(request.dest_width), k.Height(request.dest_height), uint32_t(k.X(int32_t(request.left))),
          uint32_t(k.Y(int32_t(request.top))), uint32_t(k.X(int32_t(request.right))),
          uint32_t(k.Y(int32_t(request.bottom))), uint32_t(k.X(int32_t(request.dest_x))),
          uint32_t(k.Y(int32_t(request.dest_y)))};
}

// The reason a resolve is refused before anything is recorded: the regions
// and samples no resolve path covers (the deferred cases, which the draw layer
// stops before reaching). Null when covered. same_size: the source and the
// destination have one size.
inline const char* ResolveUncovered(const ResolveRequest& request, bool depth, uint32_t samples, bool same_size) {
  if (depth && (!same_size || request.has_rect || request.dest_x || request.dest_y))
    return "native-depth-resolve-region-uncovered";
  if (depth && (request.sample < -1 || request.sample >= int32_t(samples)))
    return "native-depth-resolve-sample-uncovered";
  if (!depth && samples > 1 && (!same_size || request.has_rect)) return "native-resolve-msaa-region-uncovered";
  return nullptr;
}

// The source box of a copy resolve: the rectangle (or the whole source)
// clipped to the source and to what fits from the destination point on.
// False when nothing is left.
struct HostBox {
  uint32_t left = 0, top = 0, right = 0, bottom = 0;
};
inline bool CopyBox(const HostResolve& resolve, bool has_rect, uint32_t source_width, uint32_t source_height,
                    uint32_t dest_width, uint32_t dest_height, HostBox& box) {
  box.left = has_rect ? resolve.left : 0;
  box.top = has_rect ? resolve.top : 0;
  box.right = std::min(has_rect ? resolve.right : source_width, source_width);
  box.bottom = std::min(has_rect ? resolve.bottom : source_height, source_height);
  box.right = std::min(box.right, box.left + dest_width - std::min(resolve.dest_x, dest_width));
  box.bottom = std::min(box.bottom, box.top + dest_height - std::min(resolve.dest_y, dest_height));
  return box.right > box.left && box.bottom > box.top;
}

// The half-pixel offset gap of a scaled resolve. The title draws with the
// D3D9 pixel center (PA_SU_VTX_CNTL.pix_center, guarded in draw.cpp), so
// g_HalfPixelOffset moves its geometry by half a guest pixel: a quad that
// starts on the guest edge 0 leaves the first host columns and rows uncovered
// above 1x (one at 2x), and what was drawn before shows through at the left
// and top of the image (fades and overlays). As the SDK's resolve with
// resolve_resolution_scale_fill_half_pixel_offset, those columns and rows of
// the resolved rectangle repeat the first surely covered one: (k + 1) / 2 at
// an integer scale, the pixels whose centers lie before k / 2 at a fraction.
inline uint32_t HalfPixelGap(float k) { return k > 1.0f ? uint32_t(std::ceil(k * 0.5f - 0.5f + 1e-4f)) : 0; }

// The copies of the resolve of box to (dest_x, dest_y), with that gap filled:
// the covered part, the gap rows from the first covered row, the gap columns
// from the first covered column, the corner from the first covered pixel. No
// copy reads a gap pixel and no two write the same texel, so they need no
// order. One copy of the whole box at 1x or when the box is not larger than
// its gap. Destinations of one or two guest rows (the 128x1 ramp of the
// title's last post-processing draw, which Neji's and Kiba's trap jutsus and
// the flashbacks read) take one copy too: their drawn row is the first, and at
// 1.5x the fill copied the undrawn second row over it, which turned the ramp
// and the whole screen black .
struct GapCopy {
  HostBox source;
  uint32_t dest_x = 0, dest_y = 0;
};
inline std::vector<GapCopy> ResolveCopies(const ResolutionScale& k, const HostBox& box, uint32_t dest_x,
                                          uint32_t dest_y, uint32_t guest_height) {
  const uint32_t gx = HalfPixelGap(k.x), gy = HalfPixelGap(k.y);
  if ((!gx && !gy) || guest_height <= 2 || box.right - box.left <= gx || box.bottom - box.top <= gy) return {{box, dest_x, dest_y}};
  const uint32_t x = box.left + gx, y = box.top + gy;
  std::vector<GapCopy> copies{{{x, y, box.right, box.bottom}, dest_x + gx, dest_y + gy}};
  for (uint32_t i = 0; i < gy; ++i) copies.push_back({{x, y, box.right, y + 1}, dest_x + gx, dest_y + i});
  for (uint32_t j = 0; j < gx; ++j) copies.push_back({{x, y, x + 1, box.bottom}, dest_x + j, dest_y + gy});
  for (uint32_t i = 0; i < gy; ++i)
    for (uint32_t j = 0; j < gx; ++j) copies.push_back({{x, y, x + 1, y + 1}, dest_x + j, dest_y + i});
  return copies;
}

}  // namespace gpu
