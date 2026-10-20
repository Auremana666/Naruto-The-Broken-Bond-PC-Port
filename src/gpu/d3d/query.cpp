// D3D queries: D3DQuery_Issue (821AF240) and D3DQuery_GetData (821AF4C8).
// The title issues occlusion queries around the draws of the objects it culls
// (820FC610) and polls them once per frame for up to 10 frames (820F4990); a
// query that never completes counts as occluded. The original Issue writes
// ZPASS_DONE events that nothing executes at the D3D boundary, so the own Issue
// fills the sample-count structures (xe_gpu_depth_sample_counts, little endian)
// as the SDK's command processor does without host queries: the begin zeroed,
// the end with query_occlusion_fake_sample_count (1000) in ZPass_A and Total_A of
// each buffer. GetData is the original on the CPU side (fence, end minus begin,
// the finished marker), without the ring kick. Event queries (type 8) complete
// with their fence. Screen-extent queries (type 10) are not covered: they are
// issued as events and read as zeros (UncoveredOperation).
#include "gpu/d3d/device.h"
#include "gpu/d3d/hooks.h"
#include "gpu/diagnostics/diagnostics.h"
#include "gpu/diagnostics/census.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fmt/format.h>
#include <rex/hook.h>

namespace gpu {
namespace {
// XDK D3DQuery: +0 device, +4 type, +16 state (2 begun, 1 ended), +20 flags
// (0x80: the result was read), +24 fence of the end, +28 the sample-count
// buffers (one per predicated tile), +148 their count. Device: +10896 the
// completed fence word, +10908 the next fence, +10940 bit 0x20 predicated
// tiling with +12748 tiles, +10941 bit 0x2 results reported as 1.
constexpr uint32_t kTypeEvent = 8, kTypeOcclusion = 9, kTypeExtent = 10;
constexpr uint32_t kFakeSamples = 1000;  // The SDK's fallback (query_occlusion_fake_sample_count).
constexpr uint32_t kQueryFinished = 0xFFFFFEED;  // Written by the original Issue, overwritten by the end counts.
constexpr uint32_t kMaxBuffers = 30;  // +28 .. +148.
std::atomic<uint64_t> issues{0}, reads{0};

// The CPU view in which GetData reads a sample-count buffer (cached physical).
uint32_t SampleCounts(uint32_t buffer) { return GuestPhysicalAlias(buffer) - 0x40000000u; }
uint32_t Little32(uint8_t* base, uint32_t a) { uint32_t v; std::memcpy(&v, base + a, 4); return v; }
void PutLittle(uint8_t* base, uint32_t a, const uint32_t (&v)[8]) { std::memcpy(base + a, v, sizeof(v)); }
// The fence of the query has passed (unsigned distance, as the original).
bool FencePassed(uint8_t* base, uint32_t device, uint32_t fence) {
  const uint32_t next = Get32(base, device + 10908), completed = Get32(base, Get32(base, device + 10896));
  return !fence || next - fence >= next - completed;
}
void CheckQuery(PPCContext& ctx, uint8_t* base, uint32_t query, const char* where) {
  if (!device_owner || !device_owner->live() || Get32(base, query) != device_owner->device())
    StopInOrder(ctx, fmt::format("{}-device", where), 88);
  // Not covered: issued as an event, GetData answers zeros.
  if (Get32(base, query + 4) == kTypeExtent) UncoveredOperation(ctx, fmt::format("{}-screen-extent", where));
  if (Get32(base, query + 148) > kMaxBuffers) StopInOrder(ctx, fmt::format("{}-buffers", where), 88);
}
}  // namespace
}  // namespace gpu

using namespace gpu;

extern "C" void __imp__sub_821AF240(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821AF240) {  // D3DQuery_Issue(query, flags: 1 end, 2 begin)
  if (!Active()) return __imp__sub_821AF240(ctx, base);
  HookScope scope("B570-QueryIssue");
  const uint32_t query = ctx.r3.u32, flags = ctx.r4.u32, device = Get32(base, query), type = Get32(base, query + 4);
  CheckQuery(ctx, base, query, "B570");
  if (flags & 2) {
    const uint32_t tiles = (Get8(base, device + 10940) & 0x20) ? Get32(base, device + 12748) : 1;
    if (tiles > kMaxBuffers) StopInOrder(ctx, "B570-tiles", 88);
    Put32(base, query + 148, tiles);
    if (type == kTypeOcclusion)
      for (uint32_t i = 0; i < tiles; ++i) PutLittle(base, SampleCounts(Get32(base, query + 28 + 4 * i)) + 32, {});
    Put8(base, query + 16, 2);
  } else if (flags & 1) {
    if (type != kTypeEvent) {
      if (type == kTypeOcclusion) {
        Put8(base, query + 20, Get8(base, query + 20) & 0x7F);
        const uint32_t end[8] = {kFakeSamples, 0, 0, 0, kFakeSamples, 0, 0, 0};  // Total_A, ..., ZPass_A.
        for (uint32_t i = 0, n = Get32(base, query + 148); i < n; ++i)
          PutLittle(base, SampleCounts(Get32(base, query + 28 + 4 * i)), end);
      }
      Put8(base, query + 16, 1);
    }
    Put32(base, query + 24, Get32(base, device + 10908));
  }
  if (issues.fetch_add(1) < 8 && Diagnostics())
    host::Record("native-d3d-query-issue", fmt::format(
        "\"query\":{},\"type\":{},\"flags\":{},\"buffers\":{},\"fence\":{},\"lr\":{},\"guest_thread\":{}", query, type,
        flags, Get32(base, query + 148), Get32(base, query + 24), ctx.lr, GuestThread()));
  ctx.r3.u64 = 0;
}

extern "C" void __imp__sub_821AF4C8(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821AF4C8) {  // D3DQuery_GetData(query, data, size, flags): 0 ready, 1 not yet
  if (!Active()) return __imp__sub_821AF4C8(ctx, base);
  HookScope scope("B7F8-QueryGetData");
  const uint32_t query = ctx.r3.u32, data = ctx.r4.u32, device = Get32(base, query), type = Get32(base, query + 4);
  CheckQuery(ctx, base, query, "B7F8");
  uint32_t result = 0, value = 0;
  const bool passed = FencePassed(base, device, Get32(base, query + 24));
  if (type == kTypeEvent) {
    Put32(base, data, value = 1);
    result = passed ? 0 : 1;
  } else if (type == kTypeOcclusion) {
    if (!passed) {
      Put32(base, data, value = 1);
      result = 1;
    } else {
      uint32_t zpass_a = 0, zpass_b = 0;  // Of the last buffer, as the original.
      for (uint32_t i = 0, n = Get32(base, query + 148); i < n; ++i) {
        const uint32_t counts = SampleCounts(Get32(base, query + 28 + 4 * i));
        zpass_a = Get32(base, counts + 16);
        zpass_b = Get32(base, counts + 20);
        value += Little32(base, counts + 16) + Little32(base, counts + 20) - Little32(base, counts + 48) -
                 Little32(base, counts + 52);
      }
      if (Get8(base, device + 10941) & 2) value = 1, zpass_a = zpass_b = 0;
      if (zpass_a == kQueryFinished && zpass_b == kQueryFinished) {
        value = 1;
        result = 1;
      } else {
        Put8(base, query + 20, Get8(base, query + 20) | 0x80);
      }
      Put32(base, data, value);
    }
  } else if (type == kTypeExtent && data) {  // The screen extent, counted by CheckQuery.
    std::memset(base + data, 0, std::min<uint32_t>(ctx.r5.u32, 64));
    result = passed ? 0 : 1;
  }
  if (reads.fetch_add(1) < 8 && Diagnostics())
    host::Record("native-d3d-query-data", fmt::format(
        "\"query\":{},\"type\":{},\"ready\":{},\"value\":{},\"fence\":{},\"lr\":{},\"guest_thread\":{}", query, type,
        result == 0, value, Get32(base, query + 24), ctx.lr, GuestThread()));
  ctx.r3.u64 = result;
}
