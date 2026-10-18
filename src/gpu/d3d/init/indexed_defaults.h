#pragma once

#include <array>
#include <cstdint>

namespace gpu {
inline constexpr unsigned kDefaultSamplerSlots = 26;
inline constexpr std::array<uint32_t, 20> kIndexedDefaultValues = {
    0, 0, 0, 0, 0, 0, 2, 0, 0, 1, 0, 0, 0, 13, 0, 0, 0, 0, 0, 1};

// Own sampler intent. No guest function address, command, texture backing,
// completion or GPU receipt is represented here. A future binding adapter
// must resolve the effective LOD limits and preserved resource fields.
struct DefaultSamplerIntent {
  enum class Address : uint8_t { Repeat };
  enum class Filter : uint8_t { Point };
  enum class Mips : uint8_t { BaseMap };
  std::array<Address, 3> address{Address::Repeat, Address::Repeat, Address::Repeat};
  Filter minification = Filter::Point, magnification = Filter::Point;
  Mips mips = Mips::BaseMap;
  float lod_bias = 0, anisotropy_bias = 0;
  uint8_t requested_min_lod = 0, requested_max_lod = 13;
  bool anisotropy = false, volume_min_linear = false, volume_mag_linear = false;
  bool force_border_w_max = false, nearest_clamp = false;
  uint8_t border_rgb = 0, tri_clamp = 0;
  int8_t gradient_adjust_h = 0, gradient_adjust_v = 0;
};

// CPU compatibility projection only. The six words retain resource fields
// outside these setters. They are never submitted to the native renderer.
struct IndexedDefaultInput {
  std::array<std::array<uint32_t, 6>, kDefaultSamplerSlots> records{};
  std::array<uint32_t, kDefaultSamplerSlots> resource_slots{};
  std::array<uint8_t, kDefaultSamplerSlots> aniso_selectors{}, volume_flags{};
  uint64_t dirty = 0;
  bool operator==(const IndexedDefaultInput&) const = default;
};
struct IndexedDefaultPlan {
  IndexedDefaultInput cpu{};
  std::array<uint8_t, kDefaultSamplerSlots> requested_min_lod{}, requested_max_lod{};
  std::array<DefaultSamplerIntent, kDefaultSamplerSlots> samplers{};
  uint64_t invalidated_slots = 0;
};

class IndexedDefaultServices final {
 public:
  bool Initialize(uint64_t generation) {
    if (!generation || generation_ || generation <= last_generation_) return false;
    generation_ = generation;
    last_generation_ = generation;
    return true;
  }
  bool Prepare(uint64_t generation, const IndexedDefaultInput& input,
               IndexedDefaultPlan& output) const {
    if (!generation || generation != generation_) return false;
    // Bound resources require their own metadata and clamping contract.
    // Eight old selector values have verified static lookup bytes. Other
    // selectors stay outside this preparation's signature.
    for (unsigned i = 0; i < kDefaultSamplerSlots; ++i)
      if (input.resource_slots[i] || input.aniso_selectors[i] >= 8) return false;
    IndexedDefaultPlan plan{};
    plan.cpu = input;
    for (unsigned i = 0; i < kDefaultSamplerSlots; ++i) {
      auto& words = plan.cpu.records[i];
      words[0] &= ~uint32_t(0x0007FC00);  // repeat in all three axes
      words[1] &= ~uint32_t(0x00000800);  // nearest clamp disabled
      // Ordered defaults 4 and 5 clear anisotropic walks and the derived
      // filter fields. Default 6 selects the base mip. Intermediate lookup
      // results are overwritten; defaults 10..12 clear volume filter flags.
      words[3] = (words[3] & ~uint32_t(0x0FF80000)) | 0x01000000;
      // With no resource, defaults 8 and 13 update only requested LOD bytes.
      // Preserve the effective LOD fields (bits 2..9); all other fields in
      // this word are set to zero by the ordered default setters.
      words[4] &= 0x000003FC;
      words[5] &= ~uint32_t(0x000001FF);
      plan.cpu.aniso_selectors[i] = 1;
      plan.cpu.volume_flags[i] &= uint8_t(0xF8);
      plan.requested_min_lod[i] = 0;
      plan.requested_max_lod[i] = 13;
      plan.invalidated_slots |= uint64_t(1) << i;
      plan.cpu.dirty |= uint64_t(0x80000000) >> i;
    }
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
