#pragma once

// The performance log of Record Graphics Log (record_pipelines): with
// REX_NATIVE_PERF_LOG set (broken_bond_app.h), one line per second of play in
// REX_NATIVE_FRAME_DIR/performance.csv, flushed as it goes so a run that does
// not close cleanly still has it: the frame times of the second, the stutters,
// where the render thread spent its time (the profile slots) and what the
// pipelines did. A header gives the machine (CPU, graphics cards, power) and
// the graphics settings.

namespace gpu::perf_log {
bool Enabled();
// At each own Swap (render thread), outside the measurement mode.
void AtSwap();
}  // namespace gpu::perf_log
