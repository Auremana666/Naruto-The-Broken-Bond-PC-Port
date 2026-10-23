#pragma once

// Byte vectors reused between threads: the constant records a draw carries
// are filled on the title thread (TakeConstants) and dropped by the engine
// with the request, about 1,700 times a frame; an allocation and a free each
// time cost the title thread about 5% of its time.
#include <cstdint>
#include <vector>

namespace gpu {
// An empty vector, with the storage of a returned one when there is one.
std::vector<uint8_t> TakeBytes();
// Keeps the vector's storage (cleared) for TakeBytes; leaves it empty.
void ReturnBytes(std::vector<uint8_t>& bytes);
}  // namespace gpu
