// Byte vectors reused between threads (byte_pool.h).
#include "gpu/common/byte_pool.h"
#include <mutex>

namespace gpu {
namespace {
constexpr size_t kKeptMax = 8192;            // Vectors kept (two frames of draws, both stages).
constexpr size_t kCapacityMax = 64 * 1024;   // Larger storage is freed.
std::mutex mutex;
std::vector<std::vector<uint8_t>> kept;
}  // namespace

std::vector<uint8_t> TakeBytes() {
  std::lock_guard lock(mutex);
  if (kept.empty()) return {};
  std::vector<uint8_t> bytes = std::move(kept.back());
  kept.pop_back();
  return bytes;
}

void ReturnBytes(std::vector<uint8_t>& bytes) {
  const size_t capacity = bytes.capacity();
  if (!capacity || capacity > kCapacityMax) {
    std::vector<uint8_t>().swap(bytes);
    return;
  }
  bytes.clear();
  {
    std::lock_guard lock(mutex);
    if (kept.size() < kKeptMax) {
      kept.push_back(std::move(bytes));
      bytes = {};
      return;
    }
  }
  std::vector<uint8_t>().swap(bytes);
}
}  // namespace gpu
