#pragma once

#include "gpu/d3d/init/scalar_defaults.h"

namespace gpu {
// Source-domain words retained for a future semantic contract. Neither word
// is admitted as a native address, resource descriptor or GPU command.
struct DefaultStatePair {
  uint32_t first = 0, second = 0;
  bool operator==(const DefaultStatePair&) const = default;
};
struct DefaultTailState {
  std::array<uint32_t, 26> bindings{};
  uint32_t configuration_field = 2;
  bool selected_pairs_valid = false;
  uint32_t selected_first = 0, selected_second = 0;
  std::array<DefaultStatePair, 3> source_pairs{};
  DefaultViewportRectangle packed_rectangle{};
  bool operator==(const DefaultTailState&) const = default;
};
struct DefaultTailPlan {
  ScalarDefaultInput cpu{};
  DefaultTailState state{};
  bool operator==(const DefaultTailPlan&) const = default;
};

// Host preparation of the tail defaults and their conditional helper only.
// Input flags are those after the caller's direct defaults (default_direct.h),
// which are not implemented here.
class DefaultTailServices final {
 public:
  bool Initialize(uint64_t generation) {
    if (!generation || generation_ || generation <= last_generation_) return false;
    generation_ = last_generation_ = generation;
    return true;
  }
  bool Prepare(uint64_t generation, const ScalarDefaultInput& input,
               DefaultTailPlan& output) const {
    if (!generation || generation != generation_) return false;
    // All rejections precede any output modification. Nonzero old bindings
    // require metadata, ownership and retirement; no pointer is followed.
    for (unsigned i = 0; i < 26; ++i)
      if (input.Word(12544 + 4 * i)) return false;
    const bool select_pairs = !(input.cpu[10940] & 0x80);
    if (select_pairs && (input.cpu[10940] & 0x30)) return false;
    DefaultTailPlan plan{};
    plan.cpu = input;
    for (unsigned i = 0; i < 26; ++i) plan.cpu.Store(12544 + 4 * i, 0);
    plan.cpu.Field(10916, 0x3000, 0x2000);
    if (select_pairs) {
      plan.state.selected_pairs_valid = true;
      plan.state.selected_first = plan.state.selected_second = 64;
      plan.cpu.Field(10920, 0x7F7F0, 0x40400);
      constexpr uint64_t dirty16 = uint64_t(1) << 41 | uint64_t(1) << 40 |
          uint64_t(1) << 39 | uint64_t(1) << 44 | uint64_t(1) << 37 | 0x1809E8;
      constexpr uint64_t dirty24 = uint64_t(1) << 35 | uint64_t(1) << 54 | 0x80000000;
      constexpr uint64_t dirty32 = uint64_t(1) << 35 | 0x80000;
      plan.cpu.Store64(16, input.DoubleWord(16) | dirty16);
      plan.cpu.Store64(24, input.DoubleWord(24) | dirty24);
      plan.cpu.Store64(32, input.DoubleWord(32) | dirty32);
      constexpr std::array<unsigned, 3> pairs{13784, 14304, 14816};
      for (unsigned i = 0; i < pairs.size(); ++i)
        plan.state.source_pairs[i] = {input.Word(pairs[i]), input.Word(pairs[i] + 4)};
      const uint32_t left_top = input.Word(10436), right_bottom = input.Word(10440);
      plan.state.packed_rectangle = {Signed15(left_top), Signed15(left_top >> 16),
          Signed15(right_bottom), Signed15(right_bottom >> 16)};
    }
    // No cursor, arena, resource retirement, dispatch, receipt or callback.
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
  static int32_t Signed15(uint32_t value) {
    const uint32_t low = value & 0x7FFF;
    return low & 0x4000 ? int32_t(low) - 0x8000 : int32_t(low);
  }
  uint64_t generation_ = 0, last_generation_ = 0;
};
}  // namespace gpu
