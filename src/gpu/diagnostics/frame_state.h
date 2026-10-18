#pragma once

// Frame-state instrument, alike in the own and the reference (Xenos) mode
// (diagnostics/frame_state.cpp).
#include <array>
#include <cstdint>
#include <rex/ppc/context.h>

namespace gpu {
// kind 0 DrawIndexedVertices, 1 DrawVertices, 2 BeginVertices.
void StateNoteDraw(PPCContext& ctx, uint8_t* base, unsigned kind);
// At each Swap entry, both modes: closes the frame that just ended. Returns the
// window index of a reference frame to capture after the original Swap, or -1.
int64_t StateAtSwap(PPCContext& ctx, uint8_t* base, uint32_t dev, bool own, uint32_t front);
// Reference mode, after the original Swap of a frame to capture.
void StateAfterSwapReference(PPCContext& ctx, uint8_t* base, uint32_t dev, const std::array<uint32_t, 6>& f,
                             int64_t k);
}  // namespace gpu
