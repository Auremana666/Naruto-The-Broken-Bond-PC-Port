#pragma once

#include <cstdint>

namespace gpu {
// Semantic frontend state, never a register file or a completion receipt.
// Hardware-specific configuration has no consumer in this local interval.
// A later operation MUST resolve these blockers before enabling consumption.
enum class InitializerServiceBlocker : uint8_t {
  ShaderFlow = 1, HardwareSequencer = 2, ClipEnhancement = 4, PerformanceContext = 8
};
class InitializerServices final {
 public:
  bool Initialize(uint64_t session_generation) {
    if (generation_ || !session_generation) return false;
    generation_ = session_generation;
    invalidated_categories_ = 0xFF; // shader, resources, constants, raster,
                                   // blend, depth/stencil, viewport, targets.
    return true;
  }
  bool Matches(uint64_t generation) const {
    return generation && generation_ == generation && invalidated_categories_ == 0xFF &&
        centroid_interpolation_ && generated_parameter_slot_ == 0 && !flat_components_ &&
        !pending_work_ && !interrupt_armed_ && !callback_pending_ && !scratch_result_valid_ &&
        blockers_ == 15;
  }
  bool Release(uint64_t generation) {
    if (!Matches(generation)) return false;
    generation_ = 0;
    invalidated_categories_ = 0;
    return true;
  }
  bool active() const { return generation_ != 0; }
  // This unit cannot enable operations. Opaque hardware configuration and
  // operation/resource contracts must be closed by later semantic adapters.
  bool CanConsume() const { return false; }
 private:
  uint64_t generation_ = 0;
  uint8_t invalidated_categories_ = 0;
  bool centroid_interpolation_ = true;
  uint8_t generated_parameter_slot_ = 0;
  uint16_t flat_components_ = 0;
  uint64_t pending_work_ = 0;
  bool interrupt_armed_ = false, callback_pending_ = false, scratch_result_valid_ = false;
  uint8_t blockers_ = uint8_t(InitializerServiceBlocker::ShaderFlow) |
      uint8_t(InitializerServiceBlocker::HardwareSequencer) |
      uint8_t(InitializerServiceBlocker::ClipEnhancement) |
      uint8_t(InitializerServiceBlocker::PerformanceContext);
};
}  // namespace gpu
