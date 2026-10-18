#pragma once

// Frame layer of the D3D boundary (d3d/frame.cpp): the own Swap, its limiter
// and frame end.
#include <atomic>
#include <cstdint>
#include <string>

namespace gpu {
extern std::atomic<uint64_t> swaps;  // Own Swaps of the frame layer.
// The limiter of the own Swap, as JSON.
std::string LimiterSummary();
}  // namespace gpu
