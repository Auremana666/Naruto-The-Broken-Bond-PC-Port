// What the title is loading (common/title_loading.h): its loading screen and
// its worlds.
//
// The screen is an object with its own thread (sub_827F8AA0, entered only as a
// thread). sub_827F83E8 shows it and sub_827F7F50 hides it; sub_827F70C0, which
// names the "LoadingNoLoc" movie, calls both, and the hide has ten callers. The
// three were paired with Rise of a Ninja's loading screen (sub_82685460 show,
// sub_82684C48 hide, sub_82685AB8 thread) by instruction similarity (0.65, 0.74
// and 0.59, each well ahead of the next candidate) and by their callees; the
// hide calls the same 67-instruction helper between the same two imports. The
// hooks bracket the show and the hide with the flag and, in the native mode,
// record each loading with its Swaps and duration.
//
// sub_827B6A18 is the loader of a world file (".wow", the string it reads at
// 0x820D6A60), paired with Rise's sub_8264B3B0 (similarity 0.65, the same
// callees in the same order): it takes the file's key in r3 and looks the file
// up with it. Nothing calls it directly. The hook notes the key before the
// load, so the pipelines of the first draws of the world are recorded with it,
// and posts the pipelines the list knows for that world to the asynchronous
// threads (shaders/pipelines.h), which compile at the highest priority while
// the loading screen is up. In Rise a world is a burst of some sixty .wow
// files and the last key of the burst identifies it; how Broken Bond groups
// its files was not measured.
#include "gpu/common/title_loading.h"
#include "gpu/host/services.h"
#include "gpu/d3d/d3d.h"
#include "gpu/d3d/frame.h"
#include "gpu/shaders/pipelines.h"
#include <chrono>
#include <fmt/format.h>
#include <rex/hook.h>

extern "C" void __imp__sub_827F83E8(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_827F7F50(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_827B6A18(PPCContext& __restrict, uint8_t*);

namespace {
std::atomic<uint64_t> loading_swap_begin{0};
std::atomic<int64_t> loading_began_ns{0};
}  // namespace

REX_HOOK_RAW(sub_827F83E8) {
  gpu::title_loadings.fetch_add(1, std::memory_order_relaxed);
  loading_swap_begin.store(gpu::swaps.load(std::memory_order_relaxed), std::memory_order_relaxed);
  loading_began_ns.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
  gpu::title_loading.store(true, std::memory_order_relaxed);
  __imp__sub_827F83E8(ctx, base);
}

REX_HOOK_RAW(sub_827F7F50) {
  __imp__sub_827F7F50(ctx, base);
  const bool was_loading = gpu::title_loading.exchange(false, std::memory_order_relaxed);
  if (!was_loading || !gpu::Active()) return;
  const auto began = std::chrono::steady_clock::time_point(
      std::chrono::steady_clock::duration(loading_began_ns.load(std::memory_order_relaxed)));
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began);
  gpu::host::Record("native-d3d-title-loading", fmt::format("\"number\":{},\"ms\":{},\"swap_begin\":{},\"swap_end\":{}",
                                                            gpu::title_loadings.load(std::memory_order_relaxed), ms.count(),
                                                            loading_swap_begin.load(std::memory_order_relaxed),
                                                            gpu::swaps.load(std::memory_order_relaxed)));
}

REX_HOOK_RAW(sub_827B6A18) {
  const uint32_t key = ctx.r3.u32;
  if (key == 0 || key == 0xFFFFFFFFu) return __imp__sub_827B6A18(ctx, base);
  gpu::world_key.store(key, std::memory_order_relaxed);
  const uint32_t number = gpu::world_loads.fetch_add(1, std::memory_order_relaxed) + 1;
  const bool active = gpu::Active();
  const uint64_t swap = gpu::swaps.load(std::memory_order_relaxed);
  const auto began = std::chrono::steady_clock::now();
  const gpu::WorldPrefetch prefetch = active ? gpu::PrefetchWorldPipelines(key) : gpu::WorldPrefetch{};
  __imp__sub_827B6A18(ctx, base);
  // A world is some sixty files (its sectors); only the ones the list knows
  // are recorded. The pipelines by world are in pipelines.json.
  if (!active || !prefetch.listed) return;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began);
  gpu::host::Record("native-d3d-world-loading",
                    fmt::format("\"number\":{},\"key\":\"{:08x}\",\"loaded\":{},\"ms\":{},\"swap\":{},"
                                "\"title_loading\":{},\"listed\":{},\"posted\":{},\"present\":{},\"unresolved\":{}",
                                number, key, ctx.r3.u32 != 0, ms.count(), swap, gpu::TitleLoading(), prefetch.listed,
                                prefetch.posted, prefetch.present, prefetch.unresolved));
}
