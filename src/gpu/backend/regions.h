// The host rectangles of the engine's operations, the same for every backend:
// the guest edges of a request mapped by the resolution scale
// (common/resolution_scale.h) and clipped to the surface they act on.
#pragma once

#include <algorithm>
#include <cstdint>
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

}  // namespace gpu
