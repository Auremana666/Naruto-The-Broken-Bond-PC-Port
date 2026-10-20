// broken_bond - ReXGlue Recompiled Project
//
// Study: the sample accounting of the title's XMA streams (music and voices),
// to test why a stream stops being fed (docs/known-issues.md). The stream
// object's refill step (822CCC00) submits the next packet only while the
// samples submitted, summed from the stream's per-packet sample table (+440),
// exceed the samples the mixer pulled (+444) by less than 2400; the samples
// the XMA decoder delivered are summed at +436. REX_XMA_STREAM_TRACE=<file>
// writes, for each stream object, a line when the refill step declines while
// that window is full (again every 5 s while it lasts) and, every 10 s, a
// snapshot of every stream refilled in the last 2 s: packets submitted and in
// the table, samples submitted, delivered and pulled, the leading samples still
// to skip and the end flag. Once every packet is submitted, a line when the
// stream reaches that point and again every 5 s while it is still refilled.
// Submitted minus delivered is what the decoder owes the stream. Times are
// milliseconds since the first record. Without the variable the hook only runs
// the original. Both renderer modes.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

#include <rex/hook.h>

extern "C" void __imp__sub_822CCC00(PPCContext& __restrict ctx, uint8_t* base);

namespace {

using Clock = std::chrono::steady_clock;

struct StreamState {
  int64_t blocked_since_ms = -1, logged_ms = -1, seen_ms = 0, ended_since_ms = -1, ended_logged_ms = -1;
};

struct Trace {
  std::mutex mutex;
  std::FILE* out = nullptr;
  Clock::time_point start;
  int64_t next_snapshot_ms = 10000;
  std::map<uint32_t, StreamState> streams;
};

Trace* Open() {
  static Trace* trace = [] {
    const char* path = std::getenv("REX_XMA_STREAM_TRACE");
    if (!path || !*path) return static_cast<Trace*>(nullptr);
    auto* t = new Trace;
    t->out = std::fopen(path, "w");
    if (!t->out) {
      delete t;
      return static_cast<Trace*>(nullptr);
    }
    t->start = Clock::now();
    return t;
  }();
  return trace;
}

uint32_t Load32(uint8_t* base, uint32_t address) {
  return _byteswap_ulong(*reinterpret_cast<uint32_t*>(base + address));
}

// A guest heap address: the stream objects and their tables live there.
bool HeapAddress(uint32_t address) { return address >= 0x40000000u && address < 0xA0000000u; }

// live: the stream is the one the hooked call works on, so its table pointer
// is valid; a snapshot does not follow it.
void WriteStream(Trace& t, uint8_t* base, uint32_t stream, const char* what, int64_t now_ms, bool live) {
  const uint32_t table = Load32(base, stream + 296);
  const uint32_t packets = Load32(base, stream + 292);
  const uint32_t total = live && HeapAddress(table) ? Load32(base, table + 4) : 0;
  const uint32_t submitted = Load32(base, stream + 440), delivered = Load32(base, stream + 436);
  const uint32_t pulled = Load32(base, stream + 444);
  std::fprintf(t.out,
               "%lld %s stream=%08X state=%u packets=%u/%u submitted=%u delivered=%u pulled=%u owed=%d "
               "window=%d skip=%u end=%u drained=%u shift=%u\n",
               static_cast<long long>(now_ms), what, stream, Load32(base, stream + 4), packets, total, submitted,
               delivered, pulled, static_cast<int32_t>(submitted - delivered),
               static_cast<int32_t>(submitted - pulled), Load32(base, stream + 432), Load32(base, stream + 304),
               Load32(base, stream + 416), Load32(base, stream + 184));
}

}  // namespace

REX_HOOK_RAW(sub_822CCC00) {
  Trace* t = Open();
  const uint32_t stream = ctx.r3.u32;
  __imp__sub_822CCC00(ctx, base);
  if (!t || !stream) return;
  const uint32_t result = ctx.r3.u32;
  std::lock_guard lock(t->mutex);
  const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t->start).count();
  auto& s = t->streams[stream];
  s.seen_ms = now_ms;
  const uint32_t submitted = Load32(base, stream + 440), pulled = Load32(base, stream + 444);
  const bool window_full = submitted > pulled && submitted - pulled >= 0x960 && Load32(base, stream + 292) >= 2;
  if (result == 0 && window_full && Load32(base, stream + 4) == 0 && Load32(base, stream + 304) == 0) {
    if (s.blocked_since_ms < 0) s.blocked_since_ms = now_ms;
    if (s.logged_ms < 0 || now_ms - s.logged_ms >= 5000) {
      s.logged_ms = now_ms;
      WriteStream(*t, base, stream, now_ms == s.blocked_since_ms ? "blocked" : "still-blocked", now_ms, true);
    }
  } else if (s.blocked_since_ms >= 0) {
    std::fprintf(t->out, "%lld unblocked stream=%08X after_ms=%lld\n", static_cast<long long>(now_ms), stream,
                 static_cast<long long>(now_ms - s.blocked_since_ms));
    s.blocked_since_ms = -1;
    s.logged_ms = -1;
  }
  // Every packet submitted (+304): the stream ends once the mixer pulled the
  // samples of its table, so samples the decoder still owes keep it going.
  if (Load32(base, stream + 304) != 0) {
    if (s.ended_since_ms < 0) s.ended_since_ms = now_ms;
    if (s.ended_logged_ms < 0 || now_ms - s.ended_logged_ms >= 5000) {
      s.ended_logged_ms = now_ms;
      WriteStream(*t, base, stream, now_ms == s.ended_since_ms ? "ended" : "still-ended", now_ms, true);
    }
  }

  if (now_ms >= t->next_snapshot_ms) {
    t->next_snapshot_ms = now_ms + 10000;
    // Only the streams refilled in the last 2 s: a released object's memory is
    // reused, so its fields are no longer a stream's.
    for (const auto& [address, state] : t->streams)
      if (now_ms - state.seen_ms <= 2000 && HeapAddress(address))
        WriteStream(*t, base, address, "snapshot", now_ms, false);
  }
  std::fflush(t->out);
}
