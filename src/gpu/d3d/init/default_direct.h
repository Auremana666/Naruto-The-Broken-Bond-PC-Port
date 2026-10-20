#pragma once

#include "gpu/d3d/init/scalar_defaults.h"
#include <optional>

namespace gpu {
// CPU after the preceding defaults, plus values saved at E610 entry.
// This host image is not an authorization to read or write guest memory.
struct DefaultDirectInput {
  ScalarDefaultInput cpu{};
  uint32_t device_address = 0;
  uint64_t entry_generation = 0;
  uint32_t entry13580 = 0, entry22280 = 0;
  std::optional<uint8_t> stack91{};
};
struct DefaultInlineElement {
  uint16_t stream = 0, offset = 0;
  uint32_t encoded_type = 0x002A23B9;
  uint8_t method = 0, usage = 0, usage_index = 0;
  bool operator==(const DefaultInlineElement&) const = default;
};
struct DefaultDirectState {
  DefaultInlineElement element{};
  uint32_t element_count = 1;
  uint32_t inline_cpu_offset = 12192;
  uint8_t preserved_device_byte12247 = 0, inherited_stack91 = 0;
  uint8_t flags = 0;
  bool call31DD8 = true;
  bool operator==(const DefaultDirectState&) const = default;
};
struct DefaultDirectPlan {
  ScalarDefaultInput cpu{};
  DefaultDirectState state{};
  bool operator==(const DefaultDirectPlan&) const = default;
};

// CPU projection of the direct defaults and the caller's final helper
// predicate, host only.
// The declaration type remains in source encoding; no native layout/GPU
// interpretation or consumer is admitted by this class.
class DefaultDirectServices final {
 public:
  bool Initialize(uint64_t generation) {
    if (!generation || generation_ || generation <= last_generation_) return false;
    generation_ = last_generation_ = generation;
    return true;
  }
  bool Prepare(uint64_t generation, const DefaultDirectInput& input,
               DefaultDirectPlan& output) const {
    if (!generation || generation != generation_ || input.entry_generation != generation ||
        !input.stack91 || !input.device_address ||
        uint64_t(input.device_address) + input.cpu.cpu.size() > (uint64_t(1) << 32)) return false;
    DefaultDirectPlan plan{};
    plan.cpu = input.cpu;
    auto& cpu = plan.cpu;
    cpu.Store(12192, 5); cpu.Store(12196, 1);
    cpu.Store(12432, 2);
    cpu.Half(12244, 0); cpu.Half(12246, 0); cpu.Store(12248, 0x002A23B9);
    cpu.cpu[12252] = cpu.cpu[12253] = cpu.cpu[12254] = 0;
    // +12255 is untouched; +12267 is the explicit inherited stack byte.
    cpu.Store(12256, 0x00FF0000); cpu.Store(12260, 0xFFFFFFFF);
    cpu.Store(12264, uint32_t(*input.stack91));
    cpu.Store(12216, 1); cpu.Store(12220, 0); cpu.Store(12240, 0);
    cpu.Store(11820, input.device_address + 12192);
    cpu.Store64(16, input.cpu.DoubleWord(16) | 0x80000);
    cpu.Store(13596, input.entry13580);
    cpu.Field(23980, 1, input.entry22280 & 1);
    cpu.Store(12716, 0);
    const uint8_t flags = input.cpu.cpu[10940];
    bool bit0 = false;
    if (!(flags & 0x0C) && !input.cpu.cpu[12187]) {
      if (flags & 0x10) {
        bit0 = true;
      } else if (flags & 0x20) {
        bit0 = true;
        // Opaque identity comparison only; no target pointer is followed.
        for (unsigned i = 0; i < 5; ++i) {
          const uint32_t target = input.cpu.Word(12440 + 4 * i);
          if (target && target != input.cpu.Word(12728 + 4 * i)) bit0 = false;
        }
      }
    }
    cpu.Store(12708, 0xFFFFFFFF); cpu.Store(12712, 0);
    cpu.Store(10932, 0); cpu.Store(10936, 0);
    cpu.cpu[10940] = uint8_t((flags & 0xFE) | uint8_t(bit0));
    plan.state.preserved_device_byte12247 = input.cpu.cpu[12255];
    plan.state.inherited_stack91 = *input.stack91;
    plan.state.flags = cpu.cpu[10940];
    plan.state.call31DD8 = !(plan.state.flags & 0x80);
    // Pointer +11820 is CPU compatibility only. Own state uses an offset,
    // count and source-domain element, never a native descriptor pointer.
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
