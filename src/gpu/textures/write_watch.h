#pragma once

// Write watch of guest physical memory (write_watch.cpp), shared by the
// textures and the buffers: per 4 KB page and 1 MB chunk, the writes the SDK
// notified since a range was armed.
#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

namespace gpu::write_watch {
constexpr uint32_t kPhysical = 0x20000000u, kPageShift = 12, kChunkShift = 20;
extern std::atomic<uint32_t> page_writes[kPhysical >> kPageShift];
extern std::atomic<uint32_t> chunk_writes[kPhysical >> kChunkShift];
extern std::atomic<uint64_t> epoch, notifications, notified_pages, unlock_ranges, armed_ranges;
void Bump(uint32_t physical, uint32_t length);
uint64_t Sum(const std::vector<std::pair<uint32_t, uint32_t>>& ranges, bool pages);
void Arm(const std::vector<std::pair<uint32_t, uint32_t>>& ranges);
}  // namespace gpu::write_watch
