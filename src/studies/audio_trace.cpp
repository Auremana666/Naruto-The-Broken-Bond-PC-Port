// broken_bond - ReXGlue Recompiled Project
//
// Study: how steadily the title produces its audio frames, to tell whether the
// crackles heard in native gameplay (docs/known-issues.md) come from frames that
// arrive late. The SDK's audio worker calls the title's render callback
// (821CBF80) once per output frame of 256 samples, about 187.5 times per second,
// after the output consumed a frame; with eight frames queued, a frame later
// than about 42 ms leaves the output to fill silence. REX_AUDIO_TRACE=<file>
// writes, every second, the callbacks of that second, their mean and longest
// duration, the longest interval between two callback starts, and how many
// intervals exceeded 21 ms (four frames) and 42 ms (the queue); every interval
// above 42 ms is also written with its time and the duration of the callback
// before it. A long callback points at the title's own work or at what it
// waits for; a long interval after a short callback points at the worker
// thread not running. Times are milliseconds since the first record. Without
// the variable the hook only runs the original. Both renderer modes.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <rex/hook.h>

extern "C" void __imp__sub_821CBF80(PPCContext& __restrict ctx, uint8_t* base);

namespace {

using Clock = std::chrono::steady_clock;

constexpr int64_t kLateUs = 21333, kStarvedUs = 42667;  // Four and eight frames of 256 samples at 48 kHz.

struct Trace {
  std::FILE* out = nullptr;
  Clock::time_point start;
  int64_t window_start_us = 0, last_start_us = -1, last_duration_us = 0;
  uint64_t callbacks = 0, late = 0, starved = 0;
  int64_t duration_sum_us = 0, duration_max_us = 0, interval_max_us = 0;
};

// The worker thread is the only caller, so the trace needs no lock.
Trace* Open() {
  static Trace* trace = [] {
    const char* path = std::getenv("REX_AUDIO_TRACE");
    if (!path || !*path) return static_cast<Trace*>(nullptr);
    auto* t = new Trace;
    t->out = std::fopen(path, "w");
    if (!t->out) {
      delete t;
      return static_cast<Trace*>(nullptr);
    }
    t->start = Clock::now();
    std::fprintf(t->out, "# second callbacks mean_us max_us max_interval_us late_21ms starved_42ms\n");
    return t;
  }();
  return trace;
}

int64_t Microseconds(const Trace& t, Clock::time_point when) {
  return std::chrono::duration_cast<std::chrono::microseconds>(when - t.start).count();
}

void Flush(Trace& t, int64_t now_us) {
  while (now_us - t.window_start_us >= 1000000) {
    std::fprintf(t.out, "%lld %llu %lld %lld %lld %llu %llu\n", static_cast<long long>(t.window_start_us / 1000000),
                 static_cast<unsigned long long>(t.callbacks),
                 static_cast<long long>(t.callbacks ? t.duration_sum_us / static_cast<int64_t>(t.callbacks) : 0),
                 static_cast<long long>(t.duration_max_us), static_cast<long long>(t.interval_max_us),
                 static_cast<unsigned long long>(t.late), static_cast<unsigned long long>(t.starved));
    t.window_start_us += 1000000;
    t.callbacks = t.late = t.starved = 0;
    t.duration_sum_us = t.duration_max_us = t.interval_max_us = 0;
  }
  std::fflush(t.out);
}

}  // namespace

REX_HOOK_RAW(sub_821CBF80) {
  Trace* t = Open();
  if (!t) return __imp__sub_821CBF80(ctx, base);
  const Clock::time_point begin = Clock::now();
  __imp__sub_821CBF80(ctx, base);
  const Clock::time_point end = Clock::now();
  const int64_t start_us = Microseconds(*t, begin), duration_us = Microseconds(*t, end) - start_us;
  if (start_us - t->window_start_us >= 1000000) Flush(*t, start_us);
  if (t->last_start_us >= 0) {
    const int64_t interval_us = start_us - t->last_start_us;
    t->interval_max_us = std::max(t->interval_max_us, interval_us);
    if (interval_us > kLateUs) ++t->late;
    if (interval_us > kStarvedUs) {
      ++t->starved;
      std::fprintf(t->out, "starved at_ms=%lld interval_us=%lld previous_callback_us=%lld\n",
                   static_cast<long long>(start_us / 1000), static_cast<long long>(interval_us),
                   static_cast<long long>(t->last_duration_us));
    }
  }
  ++t->callbacks;
  t->duration_sum_us += duration_us;
  t->duration_max_us = std::max(t->duration_max_us, duration_us);
  t->last_start_us = start_us;
  t->last_duration_us = duration_us;
}
