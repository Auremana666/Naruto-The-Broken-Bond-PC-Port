#pragma once

// Hashes of the D3D boundary: the byte FNV-1a of identities (shaders, layouts,
// the frame digest) and a fast hash of contents (textures, buffers).
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gpu {
// h continues an earlier hash (the shader key: virtual part, then microcode).
inline uint64_t Fnv64(const uint8_t* p, size_t n, uint64_t h = 0xCBF29CE484222325ull) {
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
  return h;
}
// 64-bit hash of four independent multiply-rotate lanes (the
// xxHash64 round and avalanche); much faster than the byte FNV of Fnv64.
inline uint64_t FastHash64(const uint8_t* p, size_t n, uint64_t seed) {
  constexpr uint64_t P1 = 0x9E3779B185EBCA87ull, P2 = 0xC2B2AE3D27D4EB4Full, P3 = 0x165667B19E3779F9ull,
                     P4 = 0x85EBCA77C2B2AE63ull, P5 = 0x27D4EB2F165667C5ull;
  auto rotl = [](uint64_t x, int r) { return (x << r) | (x >> (64 - r)); };
  auto round = [&](uint64_t acc, uint64_t input) { return rotl(acc + input * P2, 31) * P1; };
  auto read64 = [](const uint8_t* q) { uint64_t v; std::memcpy(&v, q, 8); return v; };
  const uint8_t* const end = p + n;
  uint64_t h = seed + P5;
  if (n >= 32) {
    uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
    for (; p + 32 <= end; p += 32) {
      v1 = round(v1, read64(p));
      v2 = round(v2, read64(p + 8));
      v3 = round(v3, read64(p + 16));
      v4 = round(v4, read64(p + 24));
    }
    h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
    for (uint64_t v : {v1, v2, v3, v4}) h = (h ^ round(0, v)) * P1 + P4;
  }
  h += n;
  for (; p + 8 <= end; p += 8) h = rotl(h ^ round(0, read64(p)), 27) * P1 + P4;
  if (p + 4 <= end) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    h = rotl(h ^ (uint64_t(v) * P1), 23) * P2 + P3;
    p += 4;
  }
  for (; p < end; ++p) h = rotl(h ^ (uint64_t(*p) * P5), 11) * P1;
  h ^= h >> 33;
  h *= P2;
  h ^= h >> 29;
  h *= P3;
  return h ^ (h >> 32);
}
}  // namespace gpu
