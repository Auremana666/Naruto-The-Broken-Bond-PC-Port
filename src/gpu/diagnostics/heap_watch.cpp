// Write watch of the title's freed heap chunks (heap_watch.h).
#include "gpu/diagnostics/heap_watch.h"
#include "gpu/common/guest_memory.h"
#include <windows.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <rex/system/thread_state.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace gpu::heap_watch {
namespace {
enum Kind : uint32_t {
  kFree = 1,         // A large chunk freed (always noted, in or out of the window).
  kWatch = 2,        // Its inner pages armed.
  kReuse = 3,        // Pages given back to an allocation over them.
  kWrite = 4,        // The first write to a watched page.
  kLockBegin = 5,    // The Lock of a resolved texture: the window opens.
  kLockEnd = 6,
  kProtectFail = 7,  // VirtualProtect refused (the pages are not watched).
  kCapped = 8,       // Too many watched pages: this chunk is not watched.
  kThumbnailFree = 9,  // A freed chunk holding a compressed thumbnail: watched in or out of the window.
  // The capture task 8211D878 (D3DXSaveTextureToFileInMemory
  // 822E1EE8 of the small resolve, then 82583C28) and the compression 82583C28(thumb),
  // which frees thumb[3], allocates size * 1.05 + 16 into it and zlib-compresses the
  // DDS into thumb[3] + 4. guest = thumb, size = thumb[3] at the event, lr = thumb[2].
  kCaptureBegin = 10,
  kCaptureEnd = 11,
  kCompressBegin = 12,
  kCompressEnd = 13,
};
// The save thumbnail buffers ([0x0001C280: the DDS size][zlib 0x789C ...]),
// watched even when freed before the window.
constexpr uint32_t kThumbnailSize = 0x0001C280;
constexpr uint32_t kMinChunk = 0x2000, kPage = 0x1000, kRing = 65536, kFrames = 14, kMaxPages = 32768;
constexpr int64_t kWindowSeconds = 90;
struct Event {
  uint32_t kind, guest, size, lr, thread, free_seq;
  int64_t qpc;
  uint32_t rip;
  std::array<uint32_t, kFrames> frames;
};
struct WatchedPage {
  DWORD old_protect;
  uint32_t free_seq;
};
bool enabled = false;
std::atomic<uint8_t*> membase{nullptr};
uintptr_t exe_base = 0, exe_end = 0;
std::array<Event, kRing> ring{};
std::atomic<uint32_t> event_count{0}, free_seq{0}, watched_count{0};
std::atomic<int64_t> window_end{0};
std::mutex watch_mutex;
std::map<uint32_t, WatchedPage> watched;  // Guest page address -> its state.
std::string directory;
int64_t qpc_frequency = 1;

int64_t Now() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}
uint32_t GuestThread() {
  auto* state = rex::runtime::ThreadState::Get();
  return state ? state->thread_id() : 0;
}
uint32_t Rva(uintptr_t address) { return address >= exe_base && address < exe_end ? uint32_t(address - exe_base) : 0; }
Event& Push(uint32_t kind, uint32_t guest, uint32_t size, uint32_t lr, uint32_t seq) {
  Event& e = ring[event_count.fetch_add(1) % kRing];
  e = Event{kind, guest, size, lr, GuestThread(), seq, Now(), 0, {}};
  return e;
}
bool InWindow() { return Now() < window_end.load(std::memory_order_relaxed); }
// The host return chain of the current thread (recompiled functions: the guest chain).
void Backtrace(Event& e) {
  void* frames[kFrames + 2] = {};
  const USHORT count = RtlCaptureStackBackTrace(2, kFrames, frames, nullptr);
  for (USHORT i = 0; i < count; ++i) e.frames[i] = Rva(uintptr_t(frames[i]));
}

// Gives back the watched pages in [begin, end) (guest), restoring their protection.
void Release(uint8_t* base, uint32_t begin, uint32_t end, uint32_t lr) {
  if (!watched_count.load(std::memory_order_relaxed)) return;
  std::lock_guard lock(watch_mutex);
  auto it = watched.lower_bound(begin & ~(kPage - 1));
  uint32_t released = 0, first = 0, seq = 0;
  while (it != watched.end() && it->first < end) {
    DWORD old = 0;
    VirtualProtect(base + it->first, kPage, it->second.old_protect, &old);
    if (!released) first = it->first, seq = it->second.free_seq;
    ++released;
    it = watched.erase(it);
  }
  if (released) {
    watched_count.fetch_sub(released, std::memory_order_relaxed);
    Push(kReuse, first, released * kPage, lr, seq);
  }
}

// A large chunk was freed: its inner pages (past the dlmalloc fields at the
// start and before the next chunk's prev_foot) become read-only in the window.
void Freed(uint8_t* base, uint32_t mem, uint32_t size, uint32_t lr, bool thumbnail) {
  const uint32_t seq = free_seq.fetch_add(1);
  Event& freed = Push(thumbnail ? kThumbnailFree : kFree, mem, size, lr, seq);
  if (thumbnail) Backtrace(freed);  // Who releases the thumbnail (the save writer).
  if (!thumbnail && !InWindow()) return;
  const uint32_t begin = (mem + 0x20 + kPage - 1) & ~(kPage - 1), end = (mem - 8 + size) & ~(kPage - 1);
  if (begin >= end) return;
  const uint32_t pages = (end - begin) / kPage;
  std::lock_guard lock(watch_mutex);
  if (watched.size() + pages > kMaxPages) {
    Push(kCapped, mem, size, lr, seq);
    return;
  }
  DWORD old = 0;
  if (!VirtualProtect(base + begin, end - begin, PAGE_READONLY, &old)) {
    Push(kProtectFail, begin, end - begin, lr, seq);
    return;
  }
  for (uint32_t page = begin; page < end; page += kPage) watched[page] = WatchedPage{old, seq};
  watched_count.fetch_add(pages, std::memory_order_relaxed);
  Push(kWatch, begin, end - begin, lr, seq);
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* pointers) {
  const EXCEPTION_RECORD* record = pointers->ExceptionRecord;
  if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2 ||
      record->ExceptionInformation[0] != 1 || !watched_count.load(std::memory_order_relaxed))
    return EXCEPTION_CONTINUE_SEARCH;
  uint8_t* base = membase.load(std::memory_order_relaxed);
  const uintptr_t host = record->ExceptionInformation[1];
  if (!base || host < uintptr_t(base) || host >= uintptr_t(base) + 0x100000000ull) return EXCEPTION_CONTINUE_SEARCH;
  const uint32_t guest = uint32_t(host - uintptr_t(base)), page = guest & ~(kPage - 1);
  uint32_t seq = 0;
  {
    std::lock_guard lock(watch_mutex);
    auto it = watched.find(page);
    if (it == watched.end()) return EXCEPTION_CONTINUE_SEARCH;
    DWORD old = 0;
    VirtualProtect(base + page, kPage, it->second.old_protect, &old);
    seq = it->second.free_seq;
    watched.erase(it);
  }
  watched_count.fetch_sub(1, std::memory_order_relaxed);
  Event& e = Push(kWrite, guest, 0, 0, seq);
  // The host return chain from the faulting context: the recompiled functions
  // call each other natively, so it is the guest call chain.
  CONTEXT context = *pointers->ContextRecord;
  e.rip = Rva(context.Rip);
  for (uint32_t i = 0; i < kFrames && context.Rip; ++i) {
    e.frames[i] = Rva(context.Rip);
    DWORD64 image = 0;
    auto* function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
    if (!function) {
      context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
      context.Rsp += 8;
      continue;
    }
    void* handler_data = nullptr;
    DWORD64 establisher = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler_data, &establisher, nullptr);
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

void Write(const char* path) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return;
  const uint32_t count = event_count.load();
  const uint32_t first = count > kRing ? count - kRing : 0;
  std::fprintf(f, "{\"events\":%u,\"qpc_frequency\":%lld,\"exe_base\":%llu,\"watched_pages\":%u,\"entries\":[", count,
               static_cast<long long>(qpc_frequency), static_cast<unsigned long long>(exe_base), watched_count.load());
  for (uint32_t i = first; i < count; ++i) {
    const Event& e = ring[i % kRing];
    std::fprintf(f, "%s{\"n\":%u,\"kind\":%u,\"guest\":%u,\"size\":%u,\"lr\":%u,\"thread\":%u,\"free_seq\":%u,"
                 "\"qpc\":%lld,\"rip\":%u,\"frames\":[", i == first ? "" : ",", i, e.kind, e.guest, e.size, e.lr,
                 e.thread, e.free_seq, static_cast<long long>(e.qpc), e.rip);
    for (uint32_t k = 0; k < kFrames; ++k) std::fprintf(f, "%s%u", k ? "," : "", e.frames[k]);
    std::fprintf(f, "]}");
  }
  std::fprintf(f, "]}\n");
  std::fclose(f);
}
}  // namespace

bool Enabled() { return enabled; }

void Install() {
  const char* text = std::getenv("REX_NATIVE_HEAP_WATCH");
  if (!text || std::string_view(text) != "1" || enabled) return;
  const char* dir = std::getenv("REX_NATIVE_FRAME_DIR");
  directory = dir && *dir ? dir : ".";
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  qpc_frequency = frequency.QuadPart;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
  exe_base = uintptr_t(dos);
  exe_end = exe_base + nt->OptionalHeader.SizeOfImage;
  AddVectoredExceptionHandler(1, Handler);  // First, before the SDK's guest handler.
  enabled = true;
  // The record is rewritten every few seconds while new events arrive, so a
  // boot that does not crash keeps it too.
  std::thread([] {
    uint32_t written = 0;
    const std::string path = directory + "/heap-watch.json";
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(3));
      const uint32_t count = event_count.load();
      if (count != written) {
        Write(path.c_str());
        written = count;
      }
    }
  }).detach();
}

void NoteResolvedLock(bool begin, uint32_t header) {
  if (!enabled) return;
  if (begin) window_end.store(Now() + kWindowSeconds * qpc_frequency, std::memory_order_relaxed);
  Push(begin ? kLockBegin : kLockEnd, header, 0, 0, free_seq.load());
}

void WriteReport(const char* path) {
  if (enabled) Write(path);
}
}  // namespace gpu::heap_watch

// 822007C8: mspace_free(ms, mem) of the title's dlmalloc.
extern "C" void __imp__sub_822007C8(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_822007C8) {
  using namespace gpu::heap_watch;
  if (!Enabled()) { __imp__sub_822007C8(ctx, base); return; }
  membase.store(base, std::memory_order_relaxed);
  const uint32_t mem = ctx.r4.u32, lr = ctx.lr;
  // The chunk's head before the free; an mmapped chunk (no in-use bits) is unmapped, not watched.
  const uint32_t head = mem ? gpu::Get32(base, mem - 4) : 0;
  const bool thumbnail = (head & ~7u) >= kMinChunk && gpu::Get32(base, mem) == kThumbnailSize &&
                         gpu::Get16(base, mem + 4) == 0x789C;
  __imp__sub_822007C8(ctx, base);
  if (mem && (head & 3) && (head & ~7u) >= kMinChunk) Freed(base, mem, head & ~7u, lr, thumbnail);
}
// The Rise save-thumbnail compression hook (82583C28) is not ported: Broken
// Bond has no counterpart of that Rise fix.
// 82200470: mspace_malloc(ms, bytes).
extern "C" void __imp__sub_82200470(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_82200470) {
  using namespace gpu::heap_watch;
  const uint32_t lr = ctx.lr;
  __imp__sub_82200470(ctx, base);
  if (Enabled() && ctx.r3.u32 && watched_count.load(std::memory_order_relaxed)) {
    const uint32_t mem = ctx.r3.u32;
    Release(base, mem - 8, mem - 8 + (gpu::Get32(base, mem - 4) & ~7u), lr);
  }
}
// 82200C38: the internal realloc (it may grow a chunk in place over a free neighbour).
extern "C" void __imp__sub_82200C38(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_82200C38) {
  using namespace gpu::heap_watch;
  const uint32_t lr = ctx.lr;
  __imp__sub_82200C38(ctx, base);
  if (Enabled() && ctx.r3.u32 && watched_count.load(std::memory_order_relaxed)) {
    const uint32_t mem = ctx.r3.u32;
    Release(base, mem - 8, mem - 8 + (gpu::Get32(base, mem - 4) & ~7u), lr);
  }
}
