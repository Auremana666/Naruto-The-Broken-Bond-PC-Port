#pragma once

// Guest memory of the D3D boundary, shared by its units: big-endian words at a
// guest address (base + address), the physical alias of a guest address and
// reads of the physical mapping (base + 4 GB), the guest thread of the caller,
// calls of original CPU-only guest functions and the Xenos endian swaps.
#include <array>
#include <cstddef>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>
#include <windows.h>
#include <rex/ppc/context.h>
#include <rex/system/thread_state.h>
#include "gpu/d3d/d3d.h"

namespace gpu {
inline uint32_t Get32(uint8_t* base, uint32_t a) { return _byteswap_ulong(*reinterpret_cast<uint32_t*>(base + a)); }
inline uint16_t Get16(uint8_t* base, uint32_t a) { return _byteswap_ushort(*reinterpret_cast<uint16_t*>(base + a)); }
inline uint8_t Get8(uint8_t* base, uint32_t a) { return base[a]; }
inline void Put32(uint8_t* base, uint32_t a, uint32_t v) { *reinterpret_cast<uint32_t*>(base + a) = _byteswap_ulong(v); }
inline void Put8(uint8_t* base, uint32_t a, uint8_t v) { base[a] = v; }
inline float GetFloat(uint8_t* base, uint32_t a) { const uint32_t v = Get32(base, a); float f; std::memcpy(&f, &v, 4); return f; }
inline void PutFloat(uint8_t* base, uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); Put32(base, a, v); }
inline void Or64(uint8_t* base, uint32_t a, uint64_t mask) {
  const uint64_t v = (uint64_t(Get32(base, a)) << 32 | Get32(base, a + 4)) | mask;
  Put32(base, a, uint32_t(v >> 32)); Put32(base, a + 4, uint32_t(v));
}
inline void And64(uint8_t* base, uint32_t a, uint64_t mask) {
  const uint64_t v = (uint64_t(Get32(base, a)) << 32 | Get32(base, a + 4)) & mask;
  Put32(base, a, uint32_t(v >> 32)); Put32(base, a + 4, uint32_t(v));
}
inline uint32_t BigEndianWord(const uint8_t* bytes) {
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | bytes[3];
}
// The two sides of guest memory. A virtual address (the 4 KB heaps below
// 0x40000000, such as the own SystemHeapAlloc areas, and every pointer the
// title writes through) is read with ReadVirtual (TranslateVirtual). A
// physical address (fetch constants, ring and resolve bases; GuestPhysicalAlias
// of an address in the physical heaps 0xA0000000 and up) is read with
// ReadPhysical. GuestPhysicalAlias of a virtual-heap address is other memory.
inline bool ReadVirtual(rex::memory::Memory* memory, uint32_t address, void* target, size_t size) {
  return host::Read(memory, address, target, size);
}
// The guest memory object for a thread without a guest context
// (the translator): attached once by a guest thread (AttachGuestMemory, at the
// engine creation), else the current thread's. The write watch arms its pages
// through it and the records read guest bytes through it.
inline std::atomic<rex::memory::Memory*>& AttachedGuestMemory() {
  static std::atomic<rex::memory::Memory*> memory{nullptr};
  return memory;
}
inline void AttachGuestMemory(rex::memory::Memory* memory) { AttachedGuestMemory().store(memory); }
inline rex::memory::Memory* GuestMemory() {
  if (auto* ts = rex::runtime::ThreadState::Get()) return ts->memory();
  return AttachedGuestMemory().load();
}
inline uint32_t GuestPhysicalAlias(uint32_t address) { return (((address >> 20) + 0x200) & 0x1000) + (address & 0x1FFFFFFF); }
// Plain copies from and into guest memory, as the title's own loads and
// stores; ReadProcessMemory and WriteProcessMemory would make a system call
// at every access. An address without memory faults into the __except and
// fails.
inline bool CopyFromGuest(void* out, const uint8_t* host, size_t size) {
  __try {
    std::memcpy(out, host, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
inline bool CopyToGuest(uint8_t* host, const uint8_t* bytes, size_t size) {
  __try {
    std::memcpy(host, bytes, size);
    return std::memcmp(host, bytes, size) == 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
inline bool ReadPhysical(uint8_t* base, uint32_t physical, uint32_t size, void* out) {
  if (physical >= 0x20000000u || size > 0x20000000u - physical) return false;
  return CopyFromGuest(out, base + 0x100000000ull + physical, size);
}
inline uint32_t GuestThread() {
  auto* ts = rex::runtime::ThreadState::Get();
  return ts ? ts->thread_id() : 0;
}

// Calls an original CPU-only guest function on the current thread. The caller's
// register file is restored afterwards; r1 is moved below the caller's frame.
using GuestFunction = void (*)(PPCContext& __restrict, uint8_t*);
inline void CallGuest(PPCContext& ctx, uint8_t* base, GuestFunction fn, uint32_t sp, uint32_t r3, uint32_t r4,
               const std::array<double, 6>* floats = nullptr, uint32_t r10 = 0) {
  std::array<uint8_t, sizeof(PPCContext)> saved{};
  std::memcpy(saved.data(), &ctx, sizeof(PPCContext));
  ctx.r1.u64 = sp;
  ctx.r3.u64 = r3;
  ctx.r4.u64 = r4;
  ctx.r10.u64 = r10;
  if (floats) {
    ctx.f1.f64 = (*floats)[0]; ctx.f2.f64 = (*floats)[1]; ctx.f3.f64 = (*floats)[2];
    ctx.f4.f64 = (*floats)[3]; ctx.f5.f64 = (*floats)[4]; ctx.f6.f64 = (*floats)[5];
  }
  fn(ctx, base);
  std::memcpy(&ctx, saved.data(), sizeof(PPCContext));
}

// Swaps a guest buffer by the Xenos endian mode (0 none, 1 8in16, 2 8in32, 3 16in32).
inline bool SwapEndian(std::vector<uint8_t>& data, uint32_t mode) {
  if (mode == 0) return true;
  if (mode == 1) {
    for (size_t i = 0; i + 2 <= data.size(); i += 2) std::swap(data[i], data[i + 1]);
    return true;
  }
  if (mode == 2) {
    for (size_t i = 0; i + 4 <= data.size(); i += 4) {
      std::swap(data[i], data[i + 3]);
      std::swap(data[i + 1], data[i + 2]);
    }
    return true;
  }
  if (mode == 3) {
    for (size_t i = 0; i + 4 <= data.size(); i += 4) {
      std::swap(data[i], data[i + 2]);
      std::swap(data[i + 1], data[i + 3]);
    }
    return true;
  }
  return false;
}
}  // namespace gpu
