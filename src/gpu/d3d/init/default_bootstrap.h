#pragma once

#include "gpu/d3d/init/default_direct.h"
#include "gpu/d3d/init/default_dispatch.h"
#include "gpu/d3d/init/default_tail.h"
#include "gpu/d3d/init/indexed_defaults.h"

namespace gpu {
struct DefaultBootstrapInput {
  ScalarDefaultInput cpu{}; // At E610 entry, not a post-default snapshot.
  uint32_t device_address = 0;
  std::optional<uint8_t> stack91{};
};
struct DefaultBootstrapPlan {
  ScalarDefaultInput cpu{};
  ScalarDefaultIntent scalar{};
  std::array<DefaultSamplerIntent, 26> samplers{};
  DefaultTailState tail{};
  DefaultDirectState direct{};
  uint64_t generation = 0, invalidated_sampler_slots = 0;
};

// Final CPU projection only. No intermediate-store equivalence, guest
// transaction, ABI, command parsing, legacy backing or GPU consumer.
class DefaultBootstrapServices final {
 public:
  bool Initialize(uint64_t generation) {
    if (!generation || generation_ || generation <= last_generation_) return false;
    // All component initializers have the same monotonic admission rule and
    // are private, so no partially initialized external state is published.
    if (!dispatch_.Initialize(generation) || !scalar_.Initialize(generation) ||
        !indexed_.Initialize(generation) || !direct_.Initialize(generation) ||
        !tail_.Initialize(generation)) return false;
    generation_ = last_generation_ = generation;
    return true;
  }
  bool Prepare(uint64_t generation, const DefaultBootstrapInput& input,
               DefaultBootstrapPlan& output) const {
    if (!generation || generation != generation_ || !input.stack91 ||
        !input.device_address || uint64_t(input.device_address) + 65536 > (uint64_t(1) << 32)) return false;
    // Only empty legacy block selections are admitted in this composition.
    // Neither word is dereferenced or decoded. Bit 80 skips the helper, but
    // this conservative bootstrap signature still excludes nonempty pairs.
    for (unsigned at : {13784u, 13788u, 14304u, 14308u, 14816u, 14820u})
      if (input.cpu.Word(at)) return false;
    DefaultBootstrapPlan plan{};
    if (!dispatch_.PrepareInstall(generation, input.cpu, plan.cpu)) return false;
    ScalarDefaultPlan scalar{};
    if (!scalar_.Prepare(generation, plan.cpu, scalar)) return false;
    plan.cpu = scalar.cpu;
    plan.scalar = scalar.intent;
    IndexedDefaultInput indexed_input{};
    for (unsigned i = 0; i < 26; ++i) {
      for (unsigned w = 0; w < 6; ++w) indexed_input.records[i][w] = plan.cpu.Word(1152 + 24 * i + 4 * w);
      indexed_input.resource_slots[i] = plan.cpu.Word(12544 + 4 * i);
      indexed_input.aniso_selectors[i] = plan.cpu.cpu[11924 + i];
      indexed_input.volume_flags[i] = plan.cpu.cpu[12002 + i];
    }
    indexed_input.dirty = plan.cpu.DoubleWord(24);
    IndexedDefaultPlan indexed{};
    if (!indexed_.Prepare(generation, indexed_input, indexed)) return false;
    for (unsigned i = 0; i < 26; ++i) {
      for (unsigned w : {0u, 1u, 3u, 4u, 5u})
        plan.cpu.Store(1152 + 24 * i + 4 * w, indexed.cpu.records[i][w]);
      plan.cpu.cpu[11924 + i] = indexed.cpu.aniso_selectors[i];
      plan.cpu.cpu[11950 + i] = indexed.requested_min_lod[i];
      plan.cpu.cpu[11976 + i] = indexed.requested_max_lod[i];
      plan.cpu.cpu[12002 + i] = indexed.cpu.volume_flags[i];
    }
    plan.cpu.Store64(24, indexed.cpu.dirty);
    plan.samplers = indexed.samplers;
    plan.invalidated_sampler_slots = indexed.invalidated_slots;
    DefaultDirectInput direct_input{};
    direct_input.cpu = plan.cpu;
    direct_input.device_address = input.device_address;
    direct_input.entry_generation = generation;
    direct_input.entry13580 = input.cpu.Word(13596);
    direct_input.entry22280 = input.cpu.Word(23980);
    direct_input.stack91 = input.stack91;
    DefaultDirectPlan direct{};
    if (!direct_.Prepare(generation, direct_input, direct)) return false;
    DefaultTailPlan tail{};
    if (!tail_.Prepare(generation, direct.cpu, tail)) return false;
    // The tail defaults move after the direct ones only in this final-image
    // preparation. Zero bindings, separate configuration words and OR
    // invalidations commute in the admitted signature; an adapter cannot use
    // this ordering for concurrent guest stores.
    plan.cpu = tail.cpu;
    plan.tail = tail.state;
    plan.direct = direct.state;
    plan.generation = generation;
    output = plan;
    return true;
  }
  bool Release(uint64_t generation) {
    if (!generation || generation != generation_) return false;
    if (!dispatch_.Release(generation) || !scalar_.Release(generation) ||
        !indexed_.Release(generation) || !direct_.Release(generation) ||
        !tail_.Release(generation)) return false;
    generation_ = 0; return true;
  }
  bool CanConsume() const { return false; }
  bool active() const { return generation_ != 0; }
 private:
  uint64_t generation_ = 0, last_generation_ = 0;
  DefaultDispatchServices dispatch_{};
  ScalarDefaultServices scalar_{};
  IndexedDefaultServices indexed_{};
  DefaultDirectServices direct_{};
  DefaultTailServices tail_{};
};

// Stores proved for this final-image projection. Word 2 in each sampler,
// legacy cursor/arena and unchanged saved rectangle are outside the mask.
constexpr bool DefaultBootstrapWrites(unsigned at, bool helper) {
  if (at >= 64 && at < 1032) return true;
  if (at >= 1152 && at < 1776) return (at - 1152) % 24 < 8 || (at - 1152) % 24 >= 12;
  if (at >= 11924 && at < 12028) return true;
  if (at >= 12544 && at < 12648) return true;
  if (at >= 10920 && at < 10924) return helper;
  constexpr std::array<std::array<unsigned, 2>, 28> ranges{{
      {16,32}, {10420,4}, {10436,8}, {10456,24}, {10493,3},
      {10497,7}, {10540,36}, {10584,20}, {10606,2}, {10616,12},
      {10680,4}, {10688,20}, {10752,4}, {10832,16}, {11848,40},
      {11892,12}, {12028,16}, {13596,4}, {23980,4}, {10916,4},
      {10932,9}, {11820,4}, {12192,8}, {12216,8}, {12240,15},
      {12256,12}, {12432,4}, {12708,12}
  }};
  for (const auto& range : ranges)
    if (at >= range[0] && at - range[0] < range[1]) return true;
  return false;
}
}  // namespace gpu
