#pragma once

// Crash evidence of a host crash: a full
// minidump written by an unhandled-exception filter when the process dies of
// an unhandled host exception (the SDK logs the guest access violation and
// continues the search), and a ring of the title's last frees (820C0018) so
// the corrupted dlmalloc chunk can be matched to the free that released it.
// REX_NATIVE_CRASH_DUMP=1 turns it on (the prepare of a case may set it); the
// dump goes to REX_NATIVE_FRAME_DIR/crash.dmp, the ring to crash-frees.json.
// Both values 1 and stack write crash-stack.txt (the symbolized host stack,
// which names the recompiled guest functions, and the guest registers); stack
// writes nothing else.
#include <cstdint>

namespace gpu::crash_dump {
void Install();
// The title's free (820C0018): one ring entry per call, any thread.
void NoteFree(uint32_t kind, uint32_t pointer, uint32_t heap, uint32_t arg4, uint32_t lr, uint32_t guest_thread);
}  // namespace gpu::crash_dump
