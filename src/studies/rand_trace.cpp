// broken_bond - ReXGlue Recompiled Project
//
// Study: the CRT's srand (8221BF20) and rand (8221BF50), whose state is per
// guest thread (_getptd() + 20, 1 on a new thread), and the world's loading
// conditions, among them the time of day the title picks per boot (see
// docs/known-issues.md). REX_RAND_TRACE=<file> writes every srand with its seed
// and the return addresses of the guest call chain and,
// for each (thread, caller) pair, the first kValuesPerCaller rand results with
// the pair's call index; every kSummaryMs a line with the call count of each
// pair. It also writes every change of the world's loading conditions (the
// time of day among them) with its call chain. Times are milliseconds since the
// first record. Without the variable the hooks only run the originals.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include <rex/hook.h>
#include <rex/system/thread_state.h>

extern "C" void __imp__sub_8221BF20(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8221BF50(PPCContext& __restrict ctx, uint8_t* base);

namespace {

constexpr uint64_t kValuesPerCaller = 64;
constexpr int64_t kSummaryMs = 5000;

struct Trace {
  std::mutex mutex;
  std::FILE* out = nullptr;
  std::chrono::steady_clock::time_point start;
  int64_t next_summary_ms = kSummaryMs;
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> calls;  // (thread, LR) -> rand calls.
};

Trace* Open() {
  static Trace* trace = [] {
    const char* path = std::getenv("REX_RAND_TRACE");
    if (!path || !*path) return static_cast<Trace*>(nullptr);
    auto* t = new Trace;
    t->out = std::fopen(path, "w");
    if (!t->out) {
      delete t;
      return static_cast<Trace*>(nullptr);
    }
    t->start = std::chrono::steady_clock::now();
    return t;
  }();
  return trace;
}

uint32_t Thread() {
  auto* state = rex::runtime::ThreadState::Get();
  return state ? state->thread_id() : 0;
}

int64_t Elapsed(const Trace& t) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t.start).count();
}

uint32_t Load32(uint8_t* base, uint32_t address) {
  return _byteswap_ulong(*reinterpret_cast<uint32_t*>(base + address));
}

// The return addresses above the caller: each frame keeps the previous stack
// pointer at +0, and its caller's LR at that pointer - 8. The walk stays inside
// the thread's stack, whose bounds the PCR (r13) holds at +0x70 (top) and +0x74.
std::string CallChain(uint8_t* base, uint32_t sp, uint32_t pcr) {
  std::string chain;
  if (!pcr) return chain;
  const uint32_t top = Load32(base, pcr + 0x70), bottom = Load32(base, pcr + 0x74);
  for (int depth = 0; depth < 10 && sp >= bottom && sp + 4 <= top; ++depth) {
    const uint32_t previous = Load32(base, sp);
    if (previous <= sp || previous > top || previous - 8 < bottom) break;
    chain += fmt::format(" {:08X}", Load32(base, previous - 8));
    sp = previous;
  }
  return chain;
}

// Caller holds the mutex.
void Summarize(Trace& t, int64_t ms) {
  if (ms < t.next_summary_ms) return;
  t.next_summary_ms = ms + kSummaryMs;
  std::fprintf(t.out, "summary %lld", static_cast<long long>(ms));
  for (const auto& [key, count] : t.calls)
    std::fprintf(t.out, " %u:%08X=%llu", key.first, key.second, static_cast<unsigned long long>(count));
  std::fprintf(t.out, "\n");
  std::fflush(t.out);
}

}  // namespace

REX_HOOK_RAW(sub_8221BF20) {
  Trace* t = Open();
  const uint32_t seed = ctx.r3.u32, lr = uint32_t(ctx.lr);
  const std::string chain = t ? CallChain(base, ctx.r1.u32, ctx.r13.u32) : std::string();
  __imp__sub_8221BF20(ctx, base);
  if (!t) return;
  std::lock_guard lock(t->mutex);
  const int64_t ms = Elapsed(*t);
  std::fprintf(t->out, "srand %lld thread=%u lr=%08X seed=%u chain=%s\n", static_cast<long long>(ms), Thread(), lr,
               seed, chain.c_str());
  std::fflush(t->out);
  Summarize(*t, ms);
}

// The loading conditions of the world (827C0980 sets one: flags 0..7, the
// time of day 12 as bits 12..15 of the word at +24, 16 and 30; 827C0A50 clears
// the word or its flag byte). The time of day (Day, Night, Morning, Day
// Overcast, Evening, Sunset in the debug menu) decides the title's look.
extern "C" void __imp__sub_827C0980(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_827C0A50(PPCContext& __restrict ctx, uint8_t* base);

static void LogCondition(const char* kind, uint32_t lr, uint32_t a, uint32_t b, uint32_t before, uint32_t after,
                  const std::string& chain) {
  Trace* t = Open();
  std::lock_guard lock(t->mutex);
  std::fprintf(t->out, "%s %lld thread=%u lr=%08X args=%u,%u word=%08X->%08X time_of_day=%u chain=%s\n", kind,
               static_cast<long long>(Elapsed(*t)), Thread(), lr, a, b, before, after, (after >> 12) & 0xF,
               chain.c_str());
  std::fflush(t->out);
}

REX_HOOK_RAW(sub_827C0980) {
  if (!Open()) { __imp__sub_827C0980(ctx, base); return; }
  const uint32_t conditions = ctx.r3.u32, index = ctx.r4.u32, value = ctx.r5.u32, lr = uint32_t(ctx.lr);
  const std::string chain = CallChain(base, ctx.r1.u32, ctx.r13.u32);
  const uint32_t before = Load32(base, conditions + 24);
  __imp__sub_827C0980(ctx, base);
  LogCondition("condition-set", lr, index, value, before, Load32(base, conditions + 24), chain);
}

REX_HOOK_RAW(sub_827C0A50) {
  if (!Open()) { __imp__sub_827C0A50(ctx, base); return; }
  const uint32_t conditions = ctx.r3.u32, flag = ctx.r4.u32, lr = uint32_t(ctx.lr);
  const std::string chain = CallChain(base, ctx.r1.u32, ctx.r13.u32);
  const uint32_t before = Load32(base, conditions + 24);
  __imp__sub_827C0A50(ctx, base);
  LogCondition("condition-reset", lr, flag, 0, before, Load32(base, conditions + 24), chain);
}

REX_HOOK_RAW(sub_8221BF50) {
  Trace* t = Open();
  const uint32_t lr = uint32_t(ctx.lr);
  __imp__sub_8221BF50(ctx, base);
  if (!t) return;
  const uint32_t value = ctx.r3.u32, thread = Thread();
  std::lock_guard lock(t->mutex);
  const int64_t ms = Elapsed(*t);
  const uint64_t index = t->calls[{thread, lr}]++;
  if (index < kValuesPerCaller) {
    std::fprintf(t->out, "rand %lld thread=%u lr=%08X index=%llu value=%u\n", static_cast<long long>(ms), thread, lr,
                 static_cast<unsigned long long>(index), value);
    std::fflush(t->out);
  }
  Summarize(*t, ms);
}
