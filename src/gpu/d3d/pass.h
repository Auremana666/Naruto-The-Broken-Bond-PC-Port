#pragma once

// Pass layer of the D3D boundary (d3d/pass.cpp): tiling, clear and
// resolve. Shared with the draw layer: the EDRAM aliasing check and the own
// backing of a bound surface inside a tiled pass.
#include <array>
#include <cstdint>

namespace gpu {
bool SharedEdramBase(uint8_t* base, uint32_t dev);
// The same check over the bound headers +12440..+12456 and the EDRAM base
// (+0x1C & 0xFFF) of each, as the draw capture copies them.
bool SharedEdramBase(const std::array<uint32_t, 5>& headers, const std::array<uint32_t, 5>& edram_bases);
uint32_t Backing(uint32_t key);
}  // namespace gpu
