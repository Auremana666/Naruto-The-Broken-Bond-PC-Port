// What the title is loading, read by the backend without depending on the
// host (header only; the hooks live in host/title_loading.cpp).
//
// TitleLoading is true from the call that shows the loading screen to the
// return of the call that hides it (sub_827F83E8 and sub_827F7F50): the
// equivalent of the loading flag the recomps read to raise the priority of the
// pipeline compilation while the player waits anyway.
//
// WorldKey is the key of the world file (".wow") the title loaded last
// (sub_827B6A18, the world file loader), 0 before the first. A pipeline
// created while a world is the last one loaded is recorded with its key; the
// list built from those records knows the pipelines of each world, and the
// next load of that world posts them before its first draw.
#pragma once

#include <atomic>
#include <cstdint>

namespace gpu {
inline std::atomic<bool> title_loading{false};
inline std::atomic<uint32_t> title_loadings{0};  // Loading screens shown since the start.
inline std::atomic<uint32_t> world_key{0};
inline std::atomic<uint32_t> world_loads{0};  // Worlds loaded since the start.
inline bool TitleLoading() { return title_loading.load(std::memory_order_relaxed); }
inline uint32_t WorldKey() { return world_key.load(std::memory_order_relaxed); }
}  // namespace gpu
