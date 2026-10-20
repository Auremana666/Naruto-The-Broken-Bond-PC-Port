#pragma once

// Frame trace of the D3D boundary (frame_trace.cpp).
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gpu {
// Frame trace (diagnosis of the black 3D world): REX_NATIVE_FRAME_TRACE=<swap>
// traces the frame that Swap closes; scene+<n> the n-th frame after the first
// one with at least 400 own draws (the Story Mode scene). Diagnosis only, on the
// render thread: the operations of the frame in order (native-d3d-frame-trace)
// and, at its Swap, the front buffer and every resolve destination of the frame
// read back (FrameTraceReadback, frame layer).
struct FrameTrace {
  uint64_t swap = 0;        // Absolute target, 0: none yet.
  int64_t scene_offset = -1;  // scene+<n>, resolved at the Swap that first closes a scene frame.
  uint64_t scene_first = 0;
  std::atomic<uint64_t> target{0};
  std::vector<uint32_t> dests;  // Resolve destinations of the traced frame.
  uint64_t ops = 0;
  // The first draw of each (VS, PS, first texture) carries the diagnosis of
  // its inputs; the textures it samples are read back at the Swap (key, levels).
  std::unordered_set<std::string> pairs;
  std::vector<std::pair<uint32_t, uint32_t>> sampled;
  // REX_NATIVE_FRAME_TRACE=key: F11 arms the trace of the next frame
  // (a moment chosen while playing), re-armed after each
  // readback, at most kKeyTraces per boot.
  bool key_mode = false, key_down = false;
  uint32_t key_traces = 0;
};
extern FrameTrace frame_trace;
bool FrameTraceActive();
uint64_t FrameTraceTarget();
// Called at each Swap n with the own draws of the frame it closes.
void FrameTraceNoteSwap(uint64_t n, uint64_t frame_draws);
// After a readback in the key mode, the trace is free for the next F11.
void FrameTraceRearm();
void TraceFrameOp(const char* op, const std::string& fields);
void TraceFrameResolveDest(uint32_t dest);
// The front buffer, the resolve destinations and the sampled copies of the
// traced frame, read back at its Swap.
void FrameTraceReadback(uint32_t front, uint64_t n);
}  // namespace gpu
