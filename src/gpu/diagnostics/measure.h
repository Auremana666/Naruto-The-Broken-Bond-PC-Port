#pragma once

// Measurement of the own frames (measure.cpp): the measurement mode
// (measure-frames.json and its summary), the pacing samples, the frame files
// and the thread census.
#include <cstdint>
#include <string>
#include <rex/ppc/context.h>

namespace gpu {
// The measurement mode: REX_NATIVE_MEASURE_SWAPS (1 to 100000) own Swaps are
// timed, from the end of the title's first loading screen (the title screen
// of a boot); then the render thread stops in order (native-d3d-measure-
// complete, 86). It does not need REX_NATIVE_DIAGNOSTICS: the profile runs
// either way, and with the diagnostics the START lot is only counted (no
// occurrence stop, census, START entry reads, isolated target, per-draw
// records or front-buffer reads). Unset: no measurement; any other value is
// refused (-1).
int64_t MeasureSwaps();
// At each own Swap entry: the frame that just closed (MeasureSwaps() > 0).
void MeasureAtSwap(PPCContext& ctx);
// The frames measured so far, written and summarized; partial at a stop
// and at the window close. Render thread only.
void WriteMeasureFrames(bool partial);
// The thread of the own Swap that samples the frames.
bool OnMeasureThread();
// Pacing samples at each Swap entry, both modes (own: the own Swap).
void PacingAtSwap(PPCContext& ctx, uint8_t* base, uint32_t dev, bool own);
}  // namespace gpu
