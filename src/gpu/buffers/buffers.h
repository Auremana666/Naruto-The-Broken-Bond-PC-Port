#pragma once

// Vertex and index buffers of the D3D boundary (buffers.cpp): hosted on the
// GPU once and refreshed by the write watch, or inline for the dynamic rings.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gpu {
constexpr uint32_t kBufferBytesMax = 64u << 20, kBufferHashBytes = 64u << 10;
struct BufferKey {
  uint32_t address = 0, bytes = 0;
  uint8_t endian = 0, index = 0, index32 = 0;
  bool operator==(const BufferKey& o) const {
    return address == o.address && bytes == o.bytes && endian == o.endian && index == o.index && index32 == o.index32;
  }
};
// The draw's use of one range: a view of the hosted buffer, or the needed
// bytes inline (an index range: whole, with its max index).
struct BufferUse {
  bool hosted = false;
  uint64_t key = 0;
  uint32_t offset = 0, bytes = 0, max_index = 0;
  std::vector<uint8_t> data;
};
bool BindBuffer(uint8_t* base, const BufferKey& k, uint32_t needed, uint64_t swap, BufferUse& use, std::string& reason);
void BufferRegistrySweep(uint64_t swap);
size_t ForgetBufferHeader(uint8_t* base, uint32_t header);
std::string BufferRegistrySummary();
extern std::atomic<uint64_t> buffer_hosted_live;
}  // namespace gpu
