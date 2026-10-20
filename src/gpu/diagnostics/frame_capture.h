#pragma once

// Front-buffer captures of the own Swap (frame_capture.cpp): the readbacks of
// the first Swaps, the START and the census, and the transition captures
// with the F9 bursts.
#include <cstdint>
#include <string>

namespace gpu {
constexpr uint64_t kFrontReadbacks = 4;
// Own presentation evidence: the own texture of the front buffer, read back and
// written as raw RGBA to REX_NATIVE_FRAME_DIR.
std::string ReadFrontBuffer(uint32_t front, uint64_t n);
// REX_NATIVE_TRANSITION_CAPTURES=<period>; 0 or unset: off.
uint64_t TransitionPeriod();
void TransitionCaptureAtSwap(uint32_t front, uint64_t n, uint64_t frame_draws);
std::string TransitionSummary();
}  // namespace gpu
