#pragma once

// A write-after-free watch of the title's heap, for a dlmalloc tree of free
// chunks corrupted by a write into a chunk after its free (from bk on), as a
// compressed save thumbnail did. It names the writer: in a window after each
// Lock of the resolved thumbnail (the
// capture of a save), the inner pages of every large chunk the title frees
// (mspace_free 822007C8) are made read-only on the host; the first write to
// such a page is caught by a vectored handler, which records the guest thread,
// the host return chain (recompiled functions: the guest call chain) and the
// address, and gives the page back. An allocation (mspace_malloc 82200470, the
// internal realloc 82200C38) that returns memory over watched pages gives them
// back first, so a legitimate reuse is not a write after the free. A freed
// chunk holding a compressed save thumbnail ([0x0001C280][zlib]) is watched in
// or out of the window.
// REX_NATIVE_HEAP_WATCH=1 turns it on; the record goes to
// REX_NATIVE_FRAME_DIR/heap-watch.json (rewritten every few seconds) and, at a
// crash, crash-heap-watch.json. Side effect: a host ReadFile into a watched
// page fails (ERROR_NOACCESS) instead of faulting.
#include <cstdint>

namespace gpu::heap_watch {
void Install();
bool Enabled();
// The Lock of a resolved texture the CPU reads (WriteBackResolve): opens the
// window of the watch (begin) and notes its end.
void NoteResolvedLock(bool begin, uint32_t header);
// The record so far, from the crash filter.
void WriteReport(const char* path);
}  // namespace gpu::heap_watch
