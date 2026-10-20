// Guest functions of the graphics library that the native mode replaces or
// guards: the internals of the device creation (the D3D boundary projects
// them), the interrupt dispatch, the memory dispatcher of the creation, the
// last-error helper and the legacy consumers of the command ring.
#include "gpu/host/internal.h"
#include "gpu/common/guest_memory.h"
#include "gpu/d3d/d3d.h"
#include <array>
#include <fmt/format.h>
#include <rex/hook.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xmemory.h>

using namespace gpu::host;

extern "C" void __imp__sub_8219FEB8(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_821F6290(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_821B8D78(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_821A5498(PPCContext& __restrict, uint8_t*);

// Device creation (821B4060) internals: the D3D boundary replaces them
// (gpu::Device*); outside the native mode the original runs.
REX_HOOK_RAW(sub_8219FEB8) {
  if (gpu::Active()) {
    gpu::DeviceArena(ctx, base);
    return;
  }
  __imp__sub_8219FEB8(ctx, base);
}

REX_HOOK_RAW(sub_821B8D78) {
  if (gpu::Active()) {
    gpu::DeviceWorkers(ctx, base);
    return;
  }
  __imp__sub_821B8D78(ctx, base);
}

REX_HOOK_RAW(sub_821A5498) {
  if (gpu::Active()) {
    gpu::DeviceCache(ctx, base);
    return;
  }
  __imp__sub_821A5498(ctx, base);
}

extern "C" void __imp__sub_821B7570(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821B7570) {
  if (gpu::Active()) {
    gpu::DeviceInitializer(ctx, base);
    return;
  }
  __imp__sub_821B7570(ctx, base);
}

extern "C" void __imp__sub_821B3BD8(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821B3BD8) {
  if (gpu::Active()) {
    gpu::DeviceVideo(ctx, base);
    return;
  }
  __imp__sub_821B3BD8(ctx, base);
}

extern "C" void __imp__sub_821B38F8(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821B38F8) {
  if (gpu::Active()) {
    gpu::DeviceDefaults(ctx, base);
    return;
  }
  __imp__sub_821B38F8(ctx, base);
}

// A registered interrupt handler is not an admitted guest dispatch: the own
// Swap paces itself and no own interrupt is delivered. In particular, the
// legacy scratch-backed callback sentinel is not a receipt.
extern "C" void __imp__sub_8219FCA8(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8219FCA8) {
  if (enabled.load())
    Stop("2F950-guest-interrupt-dispatch-uncovered", &ctx, ctx.r3.u32, ctx.r4.u32, 88);
  __imp__sub_8219FCA8(ctx, base);
}
// The memory dispatcher at the natural caller of the device creation (LR
// 821B41C8); its other uses are unchanged, including explicit owner free calls.
REX_HOOK_RAW(sub_821F6290) {
  if (gpu::Active() && ctx.lr == 0x821B41C8) {
    gpu::DeviceMemory(ctx, base);
    return;
  }
  __imp__sub_821F6290(ctx, base); // Earlier memory paths retain original behavior.
}

// DD18 -> 80568 reaches this tail helper on allocation failure. F558 may
// mutate KTHREAD.last_error; restoring PPCContext cannot undo that store.
// Stop before its effects; this inner frame cannot prove joins or cleanup.
extern "C" void __imp__sub_821F7F28(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821F7F28) {
  if (enabled.load()) {
    // 80568's allocation failure is the guarded inner frame. This same pure
    // CPU helper is also used to restore per-thread last_error on normal paths.
    if (ctx.lr == 0x821F8FB4)
      Stop("native-allocation-error-helper-uncovered", &ctx, ctx.r3.u32, ctx.lr, 88);
    auto* ts = rex::runtime::ThreadState::Get();
    auto* thread = rex::system::XThread::GetCurrentThread();
    const uint32_t pcr = ctx.r13.u32, lr = ctx.lr, value = ctx.r3.u32;
    std::array<uint8_t, 4> raw_dpc{}, raw_thread{}, raw_error{};
    if (!ts || !thread || ts->context() != &ctx || ts->memory()->virtual_membase() != base ||
        pcr != thread->pcr_ptr() ||
        !Read(ts->memory(), pcr + 336, raw_dpc.data(), 4) ||
        !Read(ts->memory(), pcr + 256, raw_thread.data(), 4) ||
        gpu::BigEndianWord(raw_thread.data()) != thread->guest_object() ||
        !Read(ts->memory(), thread->guest_object() + 352, raw_error.data(), 4))
      Stop("cpu-last-error-context-uncovered", &ctx, 0, 0, 88);
    const uint32_t before = gpu::BigEndianWord(raw_error.data()), dpc = gpu::BigEndianWord(raw_dpc.data());
    __imp__sub_821F7F28(ctx, base); // Preserve original CPU write and volatile ABI effects.
    std::array<uint8_t, 4> after{};
    const bool valid = ctx.lr == lr && ctx.r13.u32 == pcr &&
        Read(ts->memory(), thread->guest_object() + 352, after.data(), 4) &&
        gpu::BigEndianWord(after.data()) == (dpc ? before : value);
    if (KeepRecord(last_error_records)) Record("native-cpu-last-error-preserved", fmt::format(
        "\"lr\":{},\"guest_thread\":{},\"cpu\":{},\"pcr\":{},\"kthread\":{},"
        "\"code\":{},\"dpc_active\":{},\"before\":{},\"after\":{},\"original_executed\":true,\"effect_verified\":{}",
        lr, thread->thread_id(), thread->active_cpu(), pcr, thread->guest_object(),
        value, dpc, before, gpu::BigEndianWord(after.data()), valid));
    if (!valid) Stop("cpu-last-error-effect-unproved", &ctx, 0, 0, 88);
    return;
  }
  __imp__sub_821F7F28(ctx, base);
}

#define GUARDED_LEGACY(name) \
  extern "C" void __imp__##name(PPCContext& __restrict, uint8_t*); \
  REX_HOOK_RAW(name) { \
    if (enabled.load()) Stop("legacy-consumer-" #name, &ctx); \
    __imp__##name(ctx, base); \
  }
GUARDED_LEGACY(sub_8219F0B0)
GUARDED_LEGACY(sub_8219F1A0)
GUARDED_LEGACY(sub_8219EFE8)
extern "C" void __imp__sub_8219F890(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8219F890) {
  if (gpu::Active()) {
    gpu::RingReserve(ctx, base);
    return;
  }
  __imp__sub_8219F890(ctx,base);
}
GUARDED_LEGACY(sub_8219F7B8)
// 8219FA88 only writes packets at its cursor argument; inside an own discard
// scope of the D3D boundary those packets reach no consumer.
extern "C" void __imp__sub_8219FA88(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8219FA88) {
  if (enabled.load() && !gpu::AdmitDiscardLeaf(ctx)) Stop("legacy-consumer-sub_8219FA88", &ctx);
  __imp__sub_8219FA88(ctx, base);
}
GUARDED_LEGACY(sub_821B77B8)
GUARDED_LEGACY(sub_821B88D8)
GUARDED_LEGACY(sub_821B8428)
GUARDED_LEGACY(sub_821B8250)
// F950 uses the explicit interrupt-dispatch guard above.
GUARDED_LEGACY(sub_821A51A8)
GUARDED_LEGACY(sub_821A61E0)
GUARDED_LEGACY(sub_821B42C8)
#undef GUARDED_LEGACY
