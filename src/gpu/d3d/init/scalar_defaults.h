#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace gpu {
// Private CPU compatibility image, never a native command or guest memory.
// A runtime adapter must capture current inputs and transact only the proved
// footprint. The complete image here is for host projection/preservation.
struct ScalarDefaultInput {
  std::array<uint8_t, 65536> cpu{};
  uint32_t Word(unsigned at) const {
    return uint32_t(cpu[at]) << 24 | uint32_t(cpu[at + 1]) << 16 |
           uint32_t(cpu[at + 2]) << 8 | cpu[at + 3];
  }
  uint64_t DoubleWord(unsigned at) const {
    return uint64_t(Word(at)) << 32 | Word(at + 4);
  }
  void Store(unsigned at, uint32_t v) {
    cpu[at] = uint8_t(v >> 24); cpu[at + 1] = uint8_t(v >> 16);
    cpu[at + 2] = uint8_t(v >> 8); cpu[at + 3] = uint8_t(v);
  }
  void Half(unsigned at, uint16_t v) {
    cpu[at] = uint8_t(v >> 8); cpu[at + 1] = uint8_t(v);
  }
  void Store64(unsigned at, uint64_t v) {
    Store(at, uint32_t(v >> 32)); Store(at + 4, uint32_t(v));
  }
  void Field(unsigned at, uint32_t mask, uint32_t value) {
    Store(at, (Word(at) & ~mask) | value);
  }
  bool operator==(const ScalarDefaultInput&) const = default;
};

// Producer-space rectangle: conversion truncates each viewport component
// separately before adding width/height. These coordinates are not yet
// admitted as a D3D12 scissor (target extent and consumers remain open).
struct DefaultViewportRectangle {
  int32_t left = 0, top = 0, right = 0, bottom = 0;
  bool operator==(const DefaultViewportRectangle&) const = default;
};
struct ScalarDefaultIntent {
  bool depth_requested = true, depth_effective = false;
  bool depth_write_requested = true, stencil_requested = false;
  bool blending = false, separate_alpha_blending = false;
  bool scissor_requested = false;
  std::array<uint8_t, 4> color_write_requested{15, 15, 15, 15};
  std::array<uint8_t, 4> color_write_effective{};
  std::array<float, 4> blend_constant{1, 1, 1, 1};
  DefaultViewportRectangle viewport_rectangle{};
  bool operator==(const ScalarDefaultIntent&) const = default;
};
struct ScalarDefaultPlan {
  ScalarDefaultInput cpu{};
  ScalarDefaultIntent intent{};
  bool operator==(const ScalarDefaultPlan&) const = default;
};

class ScalarDefaultServices final {
 public:
  bool Initialize(uint64_t generation) {
    if (!generation || generation_ || generation <= last_generation_) return false;
    generation_ = last_generation_ = generation;
    return true;
  }
  bool Prepare(uint64_t generation, const ScalarDefaultInput& input,
               ScalarDefaultPlan& output) const {
    if (!generation || generation != generation_) return false;
    // Bound targets may be mutated by setters 77..80; their ownership and
    // format contracts are open. Multi-rectangle 28270 paths are also open.
    for (unsigned i = 0; i < 5; ++i)
      if (input.Word(12440 + 4 * i)) return false;
    if (input.cpu[10940] & 0x30) return false;
    std::array<int32_t, 4> viewport{};
    for (unsigned i = 0; i < 4; ++i) {
      const float value = std::bit_cast<float>(input.Word(12648 + 4 * i));
      // This host signature makes conversion and addition defined in C++.
      // No NaN, overflow or unproved PPC saturation case is admitted.
      if (!std::isfinite(value) || value < -1048576 || value > 1048576) return false;
      viewport[i] = int32_t(value);
    }
    ScalarDefaultPlan plan{};
    plan.cpu = input;
    auto& cpu = plan.cpu;
    plan.intent.viewport_rectangle = {viewport[0], viewport[1],
        viewport[0] + viewport[2], viewport[1] + viewport[3]};
    const auto& rect = plan.intent.viewport_rectangle;
    // 50 -> AB00, default disables intersection with the saved rectangle.
    // AB00 preserves the old saved rectangle and the top bit of each half.
    cpu.Store(11856, 0);
    cpu.Field(10436, 0x7FFF7FFF,
        (uint32_t(rect.top) & 0x7FFF) << 16 | (uint32_t(rect.left) & 0x7FFF));
    cpu.Field(10440, 0x7FFF7FFF,
        (uint32_t(rect.bottom) & 0x7FFF) << 16 | (uint32_t(rect.right) & 0x7FFF));
    // The sequence 15/16 disables both blend controls before 18..23.
    cpu.Field(11848, 0x1FFF1FFF, 0x00010001);
    cpu.Field(11852, 0xFFFE0000, 0x00240000);  // 15/16/95/96/97
    for (unsigned at : {10552u, 10584u, 10588u, 10592u}) cpu.Store(at, 0x00010001);
    for (unsigned at : {10464u, 10468u, 10472u, 10476u}) cpu.Store(at, 0x3F800000);
    cpu.Store(10500, 0);
    cpu.Field(10548, 0xFFFFFFF7, 0x00700734);  // depth/stencil; bit 3 retained
    cpu.Field(10556, 0xFF00001F, 0x87000007);
    cpu.Field(10560, 0x0000FF2C, 0);
    cpu.Field(10564, 0x0001003F, 0);
    cpu.Field(10568, 0x00209FFF, 0x00008006);  // 51 then 52 clears both bias enables
    cpu.Store(10572, 1087);
    cpu.Store(10420, 0);
    cpu.Store(10456, 0xFFFF);
    cpu.Field(10460, 0xFFFF, 0);  // null targets: requested masks remain 15
    cpu.Store(10540, 0); cpu.Store(10544, 0);
    for (unsigned at : {10493u, 10494u, 10497u, 10498u}) cpu.cpu[at] = 0xFF;
    cpu.cpu[10495] = 0; cpu.cpu[10499] = 0;
    cpu.Half(10596, 8); cpu.Half(10598, 8);
    cpu.Half(10600, 1024); cpu.Half(10602, 16); cpu.Half(10606, 8);
    cpu.Field(10616, 3, 1);
    cpu.Store(10620, 0x3F800000); cpu.Store(10624, 0x3F800000);
    cpu.Field(10680, 0x400, 0x400); cpu.Field(10688, 1, 0);
    cpu.Store(10692, 0x40000000); cpu.Store(10696, 0x3F800000);
    cpu.Store(10700, 0x40000000); cpu.Store(10704, 0x3F800000);
    cpu.Store(10752, 0xFFFF);
    for (unsigned at : {10832u, 10836u, 10840u, 10844u}) cpu.Store(at, 0);
    for (unsigned at : {11860u, 11864u, 11868u, 11872u}) cpu.Store(at, 15);
    cpu.Store(11876, 1); cpu.Store(11880, 0); cpu.Store(11884, 0);
    cpu.Store(11892, 0x3F800000); cpu.Store(11896, 0x3F800000);
    cpu.Store(11900, 0x42800000);
    for (unsigned at : {12028u, 12032u, 12036u, 12040u}) cpu.Store(at, 0);
    cpu.Store(13596, 0); cpu.Field(23980, 1, 1);
    constexpr uint64_t dirty16 = uint64_t(15) << 33 | uint64_t(1) << 37 |
        uint64_t(1) << 38 | uint64_t(1) << 44 | 0x38063FE7;
    constexpr uint64_t dirty24 = uint64_t(1) << 47 | uint64_t(1) << 48 |
        uint64_t(1) << 49 | uint64_t(1) << 52 | uint64_t(1) << 53 | uint64_t(1) << 54;
    constexpr uint64_t dirty32 = uint64_t(1) << 32 | uint64_t(1) << 33 |
        uint64_t(1) << 34 | uint64_t(1) << 35 | uint64_t(1) << 37 |
        uint64_t(1) << 42 | uint64_t(1) << 43 | uint64_t(1) << 44 |
        uint64_t(1) << 45 | 0x80080000;
    cpu.Store64(16, input.DoubleWord(16) | dirty16);
    cpu.Store64(24, input.DoubleWord(24) | dirty24);
    cpu.Store64(32, input.DoubleWord(32) | dirty32);
    const uint64_t conditional = input.DoubleWord(40);
    cpu.Store64(40, input.cpu[10943] & 0x20 ? conditional | 0x100 : conditional & ~uint64_t(0x100));
    // No arena write, cursor advance, guest dispatch, callback or receipt.
    output = plan;
    return true;
  }
  bool Release(uint64_t generation) {
    if (!generation || generation != generation_) return false;
    generation_ = 0;
    return true;
  }
  bool active() const { return generation_ != 0; }
  bool CanConsume() const { return false; }
 private:
  uint64_t generation_ = 0, last_generation_ = 0;
};
}  // namespace gpu
