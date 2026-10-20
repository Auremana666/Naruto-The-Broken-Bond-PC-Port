// Sampling profiler of the title's threads (sampler.h).
#include "gpu/diagnostics/sampler.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gpu::sampler {
namespace {
// samples.bin: records of (u32 kind, u32 payload bytes, payload), little endian.
// Header: magic, period (us). Module: base u64, size u32, UTF-8 path. Thread:
// slot u32, id u32, tag char[16]. Swap: time ns u64, own draws so far u64.
// Sample: time ns u64, slot u32, depth u32, return addresses u64 x depth
// (the first is the instruction pointer).
enum Kind : uint32_t { kHeader = 1, kModule = 2, kThread = 3, kSwap = 4, kSample = 5 };
constexpr unsigned kMaxThreads = 8, kMaxFrames = 64;
constexpr size_t kStackCopy = 256 * 1024, kSlack = 64 * 1024;

struct Target {
  HANDLE handle = nullptr;
  DWORD id = 0;
  ULONG_PTR low = 0, high = 0;
  char tag[16] = {};
};
Target targets[kMaxThreads];
std::atomic<unsigned> target_count{0};
std::mutex mutex;  // Registration and the Swap queue.
std::vector<std::pair<uint64_t, uint64_t>> swap_queue;
std::once_flag started;

uint64_t NowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
uint32_t Period() {
  static const uint32_t value = [] {
    const char* text = std::getenv("REX_NATIVE_SAMPLE_PERIOD_US");
    const long v = text && *text ? std::strtol(text, nullptr, 10) : 0;
    return v <= 0 ? 0u : uint32_t(std::clamp<long>(v, 100, 100000));
  }();
  return value;
}

void Put(std::FILE* out, uint32_t kind, const void* data, uint32_t bytes) {
  const uint32_t head[2] = {kind, bytes};
  std::fwrite(head, 4, 2, out);
  if (bytes) std::fwrite(data, 1, bytes, out);
}

void PutModules(std::FILE* out, std::set<uint64_t>& known) {
  HMODULE modules[1024];
  DWORD needed = 0;
  if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) return;
  for (DWORD i = 0; i < std::min<DWORD>(needed / sizeof(HMODULE), 1024); ++i) {
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info))) continue;
    const uint64_t base = uint64_t(info.lpBaseOfDll);
    if (!known.insert(base).second) continue;
    wchar_t wide[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(modules[i], wide, DWORD(std::size(wide)));
    char path[MAX_PATH * 6];
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, int(n), path, int(sizeof(path)), nullptr, nullptr);
    std::vector<uint8_t> record(12 + size_t(std::max(bytes, 0)));
    const uint32_t size = info.SizeOfImage;
    std::memcpy(record.data(), &base, 8);
    std::memcpy(record.data() + 8, &size, 4);
    if (bytes > 0) std::memcpy(record.data() + 12, path, size_t(bytes));
    Put(out, kModule, record.data(), uint32_t(record.size()));
  }
}

// Unwinds a context whose stack was copied to [copy, copy + bytes) from
// original_rsp: values that point into the original stack are rebased to the
// copy, and a fault on a bad frame ends the walk (plain function for __try).
uint32_t Unwind(CONTEXT* ctx, uint8_t* copy, size_t bytes, uint64_t original_rsp, uint64_t* frames) {
  const uint64_t delta = uint64_t(copy) - original_rsp, begin = uint64_t(copy), end = begin + bytes;
  DWORD64* regs[] = {&ctx->Rsp, &ctx->Rbp, &ctx->Rbx, &ctx->Rsi, &ctx->Rdi, &ctx->R12, &ctx->R13, &ctx->R14, &ctx->R15};
  uint32_t depth = 0;
  frames[depth++] = ctx->Rip;
  __try {
    for (DWORD64* r : regs)
      if (*r >= original_rsp && *r < original_rsp + bytes) *r += delta;
    while (depth < kMaxFrames) {
      if (ctx->Rsp < begin || ctx->Rsp + 8 > end) break;
      DWORD64 image = 0;
      PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(ctx->Rip, &image, nullptr);
      if (function) {
        PVOID handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx->Rip, function, ctx, &handler_data, &establisher, nullptr);
      } else {  // A leaf without unwind data: the return address is at rsp.
        ctx->Rip = *reinterpret_cast<const DWORD64*>(ctx->Rsp);
        ctx->Rsp += 8;
      }
      if (!ctx->Rip) break;
      for (DWORD64* r : regs)
        if (*r >= original_rsp && *r < original_rsp + bytes) *r += delta;
      frames[depth++] = ctx->Rip;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return depth;
}

struct Sample {
  uint64_t time;
  uint32_t slot, depth;
  uint64_t frames[kMaxFrames];
};

void Run() {
  SetThreadDescription(GetCurrentThread(), L"Native Sampler");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  const char* dir = std::getenv("REX_NATIVE_FRAME_DIR");
  const std::string path = std::string(dir && *dir ? dir : ".") + "/samples.bin";
  std::FILE* out = std::fopen(path.c_str(), "wb");
  if (!out) return;
  static char buffer[1 << 20];
  std::setvbuf(out, buffer, _IOFBF, sizeof(buffer));
  const uint64_t header[2] = {0x314C504D41535250ull /* "PRSAMPL1" */, Period()};
  Put(out, kHeader, header, sizeof(header));
  std::set<uint64_t> modules;
  PutModules(out, modules);
  auto* copy = static_cast<uint8_t*>(VirtualAlloc(nullptr, kStackCopy + kSlack, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!copy || !timer) {
    std::fclose(out);
    return;
  }
  unsigned threads_written = 0;
  uint64_t last_flush = NowNs(), last_modules = last_flush;
  std::vector<std::pair<uint64_t, uint64_t>> swaps;
  static Sample sample;
  for (;;) {
    LARGE_INTEGER due;
    due.QuadPart = -int64_t(Period()) * 10;
    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, INFINITE);
    const unsigned count = target_count.load(std::memory_order_acquire);
    for (; threads_written < count; ++threads_written) {
      const Target& t = targets[threads_written];
      uint8_t record[4 + 4 + 16];
      std::memcpy(record, &threads_written, 4);
      std::memcpy(record + 4, &t.id, 4);
      std::memcpy(record + 8, t.tag, 16);
      Put(out, kThread, record, sizeof(record));
    }
    {
      std::lock_guard lock(mutex);
      swaps.swap(swap_queue);
    }
    for (const auto& s : swaps) {
      const uint64_t record[2] = {s.first, s.second};
      Put(out, kSwap, record, sizeof(record));
    }
    swaps.clear();
    for (unsigned slot = 0; slot < count; ++slot) {
      const Target& t = targets[slot];
      CONTEXT ctx{};
      ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
      if (SuspendThread(t.handle) == DWORD(-1)) continue;
      const bool ok = GetThreadContext(t.handle, &ctx) != 0;
      sample.time = NowNs();
      size_t bytes = 0;
      const uint64_t rsp = ctx.Rsp;
      if (ok && rsp >= t.low && rsp < t.high) {
        bytes = std::min<size_t>(size_t(t.high - rsp), kStackCopy);
        std::memcpy(copy, reinterpret_cast<const void*>(rsp), bytes);
      }
      ResumeThread(t.handle);
      if (!ok) continue;
      sample.slot = slot;
      if (bytes) {
        sample.depth = Unwind(&ctx, copy, bytes, rsp, sample.frames);
      } else {
        sample.frames[0] = ctx.Rip;
        sample.depth = 1;
      }
      Put(out, kSample, &sample, uint32_t(16 + 8 * sample.depth));
    }
    const uint64_t now = NowNs();
    if (now - last_modules > 10'000'000'000ull) {
      PutModules(out, modules);
      last_modules = now;
    }
    if (now - last_flush > 1'000'000'000ull) {  // The process may end by a hard exit.
      std::fflush(out);
      last_flush = now;
    }
  }
}
}  // namespace

bool Enabled() { return Period() != 0; }

void RegisterThread(const char* tag) {
  if (!Enabled()) return;
  thread_local bool registered = false;
  if (registered) return;
  registered = true;
  {
    std::lock_guard lock(mutex);
    const unsigned n = target_count.load(std::memory_order_relaxed);
    if (n >= kMaxThreads) return;
    Target& t = targets[n];
    t.id = GetCurrentThreadId();
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &t.handle,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0))
      return;
    GetCurrentThreadStackLimits(&t.low, &t.high);
    std::strncpy(t.tag, tag, sizeof(t.tag) - 1);
    target_count.store(n + 1, std::memory_order_release);
  }
  std::call_once(started, [] { std::thread(Run).detach(); });
}

void NoteSwap(uint64_t own_draws) {
  if (!Enabled()) return;
  const uint64_t now = NowNs();
  std::lock_guard lock(mutex);
  swap_queue.emplace_back(now, own_draws);
}
}  // namespace gpu::sampler
