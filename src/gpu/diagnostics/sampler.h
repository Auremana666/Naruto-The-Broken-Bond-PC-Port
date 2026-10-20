#pragma once

// A sampling profiler of the
// title's threads inside the process, for the frames that do not fit 16.7 ms
// (the profile slots measure the hooks only, not the title's own time, and
// CDB pauses the process for ~0.1 s per sample). A thread of the sampler
// suspends each registered thread every period, copies its context and stack,
// resumes it and unwinds the copy (no lock is taken while the thread is
// suspended). REX_NATIVE_SAMPLE_PERIOD_US=<us> turns it on, independent of
// REX_NATIVE_DIAGNOSTICS, so a boot without the diagnostic instruments has the
// cost of the product. Output: REX_NATIVE_FRAME_DIR/samples.bin (modules,
// threads, the own Swaps with the draws so far, and the samples), to be read
// with the PDB of the executable.
#include <cstdint>

namespace gpu::sampler {
bool Enabled();
// The calling thread, once, under a tag (draw, swap, main, engine).
void RegisterThread(const char* tag);
// At each own Swap: the time and the own draws so far (the frames of the samples).
void NoteSwap(uint64_t own_draws);
}  // namespace gpu::sampler
