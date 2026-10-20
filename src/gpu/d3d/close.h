#pragma once

// Ordered close of the D3D boundary (d3d/close.cpp).
#include <rex/ppc/context.h>

namespace gpu {
// REX_NATIVE_CLOSE_TEST=fallback: the render thread takes the close request
// only 12 s after it arrived (TakeStopRequest), so the 10 s fallback runs.
bool CloseTestFallback();
// The render thread at its next own Swap or draw after the close request.
[[noreturn]] void ParkForClose(PPCContext& ctx);
}  // namespace gpu
