// Guest device of the D3D boundary: its creation (821B4060 and the internals it
// calls), the setters that serialize, Lock and BlockOnFence,
// D3DDevice_Release and the guards of the remaining serializing
// entries; the exports admitted around the own device (display gamma, HSIO,
// the SDK services of the own Swap). The owner: d3d/device.h.
#include "gpu/d3d/device.h"
#include "gpu/diagnostics/diagnostics.h"
#include "gpu/diagnostics/census.h"
#include "gpu/d3d/hooks.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dxgiformat.h>
#include <array>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
#include <dxcapi.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xmemory.h>

using namespace gpu;

namespace gpu {
// --- Device creation: 821B4060 internals ---
void DeviceArena(PPCContext& ctx, uint8_t*) { Step(ctx, device_owner && device_owner->Arena(ctx)); }
void DeviceWorkers(PPCContext& ctx, uint8_t*) { Step(ctx, device_owner && device_owner->Workers(ctx)); }
void DeviceCache(PPCContext& ctx, uint8_t*) { Step(ctx, device_owner && device_owner->Cache(ctx)); }
void DeviceInitializer(PPCContext& ctx, uint8_t*) {
  if (!host::InitializerRegistration(ctx.r3.u32))
    StopInOrder(ctx, "483D0-engine-registration-unproved", 88);
  Step(ctx, device_owner && device_owner->Initializer(ctx, host::ActiveDispatcher()));
}
void DeviceVideo(PPCContext& ctx, uint8_t* base) { Step(ctx, device_owner && device_owner->Video(ctx, base)); }
void DeviceDefaults(PPCContext& ctx, uint8_t*) { Step(ctx, device_owner && device_owner->Defaults(ctx)); }
}  // namespace gpu

extern "C" void __imp__sub_821F6290(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_8219F890(PPCContext& __restrict, uint8_t*);
namespace gpu {
void DeviceMemory(PPCContext& ctx, uint8_t* base) {
  Step(ctx, device_owner && device_owner->Memory(ctx));
  __imp__sub_821F6290(ctx, base);  // Returns at its first test for r3 == 0.
}
// Ring reserve: the unconditional flush inside 821B4060 is omitted, the
// reserve of the 82170540 tail gets the own discard buffer and Broken Bond's
// end-of-function kicks under the own Swap are skipped. Any other arrival is
// an uncovered serialization.
void RingReserve(PPCContext& ctx, uint8_t* base) {
  if (device_owner && device_owner->inside() && ctx.lr == 0x821B4224) {
    Step(ctx, device_owner->Flush(ctx));
    return;
  }
  if (AdmitSceneFlushReserve(ctx, base)) return;
  if (AdmitSwapDiscardKick(ctx, base)) return;
  StopInOrder(ctx, "F608-serialization-guard", 88);
}
// VdGetCurrentDisplayGamma(pType, pPower) is a query of the display gamma
// settings: two guest stores, no GPU effect. Admitted only from 821B4E58, the
// ramp builder of D3DDevice_SetGammaRamp (821A5008), which is CPU only (copy to
// device+15004, pending bit 1 of +16708 for the Swap).
thread_local uint32_t gamma_type_ptr = 0, gamma_power_ptr = 0;
std::atomic<uint64_t> gamma_queries{0};
// Set by the own D3DDevice_Swap around the display services it calls itself:
// VdSetDisplayMode, VdGetCurrentDisplayInformation, VdPersistDisplay, the
// display scaler services of 821433C0 (VdQueryVideoFlags, VdQueryVideoMode,
// VdInitializeScalerCommandBuffer, which writes into a guest buffer) and the
// clock-gating hint of 821C0620 keep their SDK implementation (no GPU work in
// the runtime).
thread_local bool own_swap_scope = false;
bool AdmitExport(std::string_view name, PPCContext& ctx) {
  if (Active() && own_swap_scope && (name == "__imp__VdSetDisplayMode" ||
      name == "__imp__VdGetCurrentDisplayInformation" || name == "__imp__VdPersistDisplay" ||
      name == "__imp__VdQueryVideoFlags" || name == "__imp__VdQueryVideoMode" ||
      name == "__imp__VdInitializeScalerCommandBuffer" || name == "__imp__VdEnableDisableClockGating")) {
    if (Diagnostics())
      host::Record("native-d3d-swap-sdk", fmt::format(
        "\"name\":\"{}\",\"lr\":{},\"args\":[{},{},{},{},{},{},{},{}]", name, ctx.lr, ctx.r3.u32, ctx.r4.u32,
        ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32));
    return true;
  }
  if (Active() && name == "__imp__VdGetCurrentDisplayGamma" && device_owner && device_owner->live() &&
      ctx.lr == 0x821B4E88 && ctx.r3.u32 && ctx.r4.u32) {
    gamma_type_ptr = ctx.r3.u32;
    gamma_power_ptr = ctx.r4.u32;
    return true;  // Original SDK query.
  }
  if (!Active() || name != "__imp__VdIsHSIOTrainingSucceeded" || !device_owner || !device_owner->AdmitHsio(ctx))
    return false;
  if (Diagnostics())
    host::Record("native-d3d-device-hsio-query",
      fmt::format("\"name\":\"{}\",\"guest_thread\":{},\"lr\":{},\"r1\":{},\"r13\":{},\"r3\":{}",
                  name, rex::runtime::ThreadState::Get() ? rex::runtime::ThreadState::Get()->thread_id() : 0,
                  ctx.lr, ctx.r1.u32, ctx.r13.u32, ctx.r3.u32));
  return true;  // Original SDK query; no hardware training is performed.
}
void ExportAfter(std::string_view name, PPCContext& ctx) {
  if (!Active() || name != "__imp__VdGetCurrentDisplayGamma" || !gamma_type_ptr) return;
  auto* ts = rex::runtime::ThreadState::Get();
  uint32_t type = 0, power = 0;
  ReadVirtual(ts->memory(), gamma_type_ptr, &type, 4);
  ReadVirtual(ts->memory(), gamma_power_ptr, &power, 4);
  type = _byteswap_ulong(type);
  power = _byteswap_ulong(power);
  float power_f;
  std::memcpy(&power_f, &power, 4);
  if (Diagnostics() && gamma_queries.fetch_add(1) < 4)
    host::Record("native-d3d-display-gamma", fmt::format(
        "\"guest_thread\":{},\"lr\":{},\"type\":{},\"power\":{},\"power_bits\":{}", ts->thread_id(), ctx.lr,
        type, power_f, power));
  gamma_type_ptr = gamma_power_ptr = 0;
}
}  // namespace gpu

// --- Device creation ---
extern "C" void __imp__sub_821B4060(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821B4060) {
  if (gpu::Active()) {
    if (device_owner) StopInOrder(ctx, "ECE8-device-reentry", 88);
    device_owner = std::make_unique<DeviceOwner>();
    // Whether the diagnostic instruments run in this boot.
    gpu::host::Record("native-d3d-diagnostics",
                             fmt::format("\"on\":{},\"variable\":\"REX_NATIVE_DIAGNOSTICS\"", Diagnostics()));
    Step(ctx, device_owner->Enter(ctx, base));
    __imp__sub_821B4060(ctx, base);  // Original body; serializing internals hooked.
    Step(ctx, device_owner->Returned(ctx));
    return;
  }
  __imp__sub_821B4060(ctx, base);
}
// Rows 17-19 of the 821B4060 inventory. Other callers keep the original:
// 43FF8 is CPU-only and 445D8/280B0 serialize, so they reach the F608 guard.
extern "C" void __imp__sub_821C0298(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821C0298) {
  if (gpu::Active() && ctx.lr == 0x821B4254) Step(ctx, device_owner && device_owner->Callbacks(ctx));
  __imp__sub_821C0298(ctx, base);
}
extern "C" void __imp__sub_821C0900(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821C0900) {
  if (gpu::Active() && ctx.lr == 0x821B4264) {
    Step(ctx, device_owner && device_owner->Edram(ctx));
    return;
  }
  __imp__sub_821C0900(ctx, base);
}
extern "C" void __imp__sub_821A1C58(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821A1C58) {
  if (gpu::Active() && ctx.lr == 0x821B4270) {
    Step(ctx, device_owner && device_owner->Dirty(ctx));
    return;
  }
  __imp__sub_821A1C58(ctx, base);
}

// --- State setters: SetRenderTarget, SetDepthStencilSurface and
// SetViewport serialize only through 821A1E10; their CPU setters run original.
// The first SetRenderTarget carries the device creation checkpoint.
extern "C" void __imp__sub_821A49E8(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821A49E8) {
  if (gpu::Active()) {
    HookScope scope("ADA0-SetRenderTarget");
    if (!device_owner) StopInOrder(ctx, "ADA0-without-device", 88);
    if (!device_owner->live()) {
      if (ctx.lr != 0x8215A888) StopInOrder(ctx, "ADA0-uncovered-caller", 88);
      Step(ctx, device_owner->Checkpoint(ctx));
    }
    Step(ctx, device_owner->Bind(ctx, false));
  }
  __imp__sub_821A49E8(ctx, base);
}
extern "C" void __imp__sub_821A4D30(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821A4D30) {
  if (gpu::Active()) {
    HookScope scope("B108-SetDepthStencilSurface");
    Step(ctx, device_owner && device_owner->Bind(ctx, true));
    __imp__sub_821A4D30(ctx, base);
    return;
  }
  __imp__sub_821A4D30(ctx, base);
}
extern "C" void __imp__sub_821A1E10(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821A1E10) {
  if (gpu::Active()) {
    HookScope scope("28270-scissor");
    Step(ctx, device_owner && device_owner->Scissor(ctx));
    return;
  }
  __imp__sub_821A1E10(ctx, base);
}

// Lock: on the device owner thread (+10888) the original emits a cache
// invalidate packet; on any other thread it accumulates the same CPU-written
// range in +11824/+11828 for the next submission. The D3D boundary always
// takes the accumulating path; a pending fence still reaches BlockOnFence.
extern "C" void __imp__sub_821A0558(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821A0558) {
  if (gpu::Active()) {
    HookScope scope("D950-Lock");
    gpu::profile::Timer hook_time(gpu::profile::kHookLock);
    uint32_t saved = 0;
    if (!device_owner) StopInOrder(ctx, "D950-without-device", 88);
    Step(ctx, device_owner->BeginLock(ctx, saved));
    __imp__sub_821A0558(ctx, base);
    Step(ctx, device_owner->EndLock(ctx, saved));
    return;
  }
  __imp__sub_821A0558(ctx, base);
}
// BlockOnFence: console fence protocol over the own writeback block
// at +10896 and the counter 10908; see DeviceOwner::BlockOnFence.
extern "C" void __imp__sub_8219F990(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8219F990) {
  if (gpu::Active()) {
    HookScope scope("F708-BlockOnFence");
    gpu::profile::Timer profile(gpu::profile::kFenceWait);
    if (!device_owner || !device_owner->live() || ctx.r3.u32 != device_owner->device()) StopInOrder(ctx, "F708-device", 88);
    std::string evidence, fields;
    auto wait_idle = [&] {  // Drain refuses after a failed posted operation.
      auto* dispatcher = gpu::host::ActiveDispatcher();
      return dispatcher && dispatcher->Drain(evidence);
    };
    const auto* dispatcher = gpu::host::ActiveDispatcher();
    const auto preparation_budget = std::chrono::seconds(
        dispatcher && dispatcher->Api() == GraphicsApi::kVulkan ? 60 : 2);
    const auto result = device_owner->BlockOnFence(ctx.r4.u32, wait_idle, FenceWaitFailed, fields, preparation_budget);
    if (result == DeviceOwner::FenceResult::Failed) {
      TakeStopRequest(ctx);
      RecordPostedFailure("F708-BlockOnFence");
      StopInOrder(ctx, "F708-own-fence-unproved", 88);
    }
    if (device_owner->NoteFence() <= 16 && Diagnostics()) {
      auto* ts = rex::runtime::ThreadState::Get();
      gpu::host::Record("native-d3d-fence-wait", fmt::format(
          "{},\"reason\":{},\"lr\":{},\"guest_thread\":{},\"engine\":{}", fields, ctx.r5.u32, ctx.lr,
          ts ? ts->thread_id() : 0, evidence.empty() ? "null" : evidence));
    }
    return;
  }
  if (gpu::host::Enabled()) gpu::host::Stop("legacy-consumer-sub_8219F990", ctx, 86);
  __imp__sub_8219F990(ctx, base);
}

// --- Remaining serializing entries: a natural arrival is the next
// checkpoint of the D3D boundary, never a fallback to serialization.
// Not covered: without the census they are counted and run as stubs: the
// LockBox entries run the original (CPU addresses; the game has no volume
// textures), the others return without the original (ENTRY_STUB).
#define ENTRY_GUARD(name) \
  extern "C" void __imp__##name(PPCContext& __restrict, uint8_t*); \
  REX_HOOK_RAW(name) { \
    if (gpu::Active()) UncoveredOperation(ctx, "d3d-entry-guard-" #name); \
    __imp__##name(ctx, base); \
  }
#define ENTRY_STUB(name) \
  extern "C" void __imp__##name(PPCContext& __restrict, uint8_t*); \
  REX_HOOK_RAW(name) { \
    if (gpu::Active()) { \
      UncoveredOperation(ctx, "d3d-entry-guard-" #name); \
      ctx.r3.u64 = 0; \
      return; \
    } \
    __imp__##name(ctx, base); \
  }
// D030, D118 and D530 (LockRect) reach the own 8219D6E0 above; DrawVertices
// (821AE5D0) has the own draw of the draw layer.
ENTRY_GUARD(sub_8219E138)  // D3DVolumeTexture_LockBox
ENTRY_GUARD(sub_8219E510)  // D3DVolume_LockBox
// D3DDevice_Release (821A1AE8). With the reference count (+60) above
// 1 the original only decrements it, CPU only (the D3DX-like texture clone
// 822E00A8 takes a reference through 8212DE00 and drops it here); the last
// reference destroys the device (821B42C8), which stays a guard (without the
// census the device stays).
extern "C" void __imp__sub_821A1AE8(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821A1AE8) {
  if (gpu::Active() && Get32(base, ctx.r3.u32 + 60) <= 1) {
    UncoveredOperation(ctx, "d3d-entry-guard-sub_821A1AE8");
    ctx.r3.u64 = 1;
    return;
  }
  __imp__sub_821A1AE8(ctx, base);
}
ENTRY_STUB(sub_821A15B0)  // D3DDevice_PersistDisplay
ENTRY_STUB(sub_821B2578)  // D3DDevice_Present
// D3DQuery_Issue (821AF240) and D3DQuery_GetData (821AF4C8): d3d/query.cpp.
#undef ENTRY_GUARD
