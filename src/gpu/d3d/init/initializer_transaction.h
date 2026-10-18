#pragma once

#include "gpu/d3d/init/initializer_cpu.h"

namespace gpu {
// Memory mechanics only. The retained owner supplies the session/metadata
// predicates and accessors. A failed or partial operation is terminal: this
// class deliberately provides no rollback, retry or recovery from Poisoned.
class InitializerCpuTransaction final {
 public:
  enum class Phase { Empty, Captured, Committed, Restored, Poisoned };
  InitializerCpuTransaction() = default;
  InitializerCpuTransaction(const InitializerCpuTransaction&) = delete;
  InitializerCpuTransaction& operator=(const InitializerCpuTransaction&) = delete;

  template <typename Authority, typename Read>
  bool Capture(Authority authority, Read read) {
    if (phase_ != Phase::Empty || !authority()) return Poison();
    // Exclude reentry even while a supplied accessor is in progress.
    phase_ = Phase::Poisoned;
    std::array<uint32_t, kInitializerCpuWords> words{};
    const auto fields = InitializerCpuLayout();
    for (size_t i = 0; i < fields.size(); ++i) {
      std::array<uint8_t, 4> bytes{};
      if (!read(fields[i].offset, bytes)) return false;
      words[i] = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
                 (uint32_t(bytes[2]) << 8) | bytes[3];
    }
    image_ = ProjectInitializerCpuImage(words);
    if (!Matches(read, image_.before) || !authority()) return false;
    phase_ = Phase::Captured;
    return true;
  }

  template <typename Authority, typename Read, typename Write>
  bool Commit(Authority authority, Read read, Write write) {
    if (phase_ != Phase::Captured || !authority() || !Matches(read, image_.before)) return Poison();
    phase_ = Phase::Poisoned;
    const auto fields = InitializerCpuLayout();
    for (size_t i = 0; i < fields.size(); ++i) {
      if (!authority() || !write(fields[i].offset, image_.projected[i])) return false;
    }
    if (!Matches(read, image_.projected) || !authority()) return false;
    phase_ = Phase::Committed;
    return true;
  }

  template <typename Authority, typename Read, typename Write>
  bool RestoreAfterJoins(Authority joined, Read read, Write write) {
    if (phase_ != Phase::Committed || !joined() || !Matches(read, image_.projected)) return Poison();
    phase_ = Phase::Poisoned;
    const auto fields = InitializerCpuLayout();
    for (size_t i = fields.size(); i > 0; --i) {
      if (!joined() || !write(fields[i - 1].offset, image_.before[i - 1])) return false;
    }
    if (!Matches(read, image_.before) || !joined()) return false;
    phase_ = Phase::Restored;
    return true;
  }

  template <typename Read>
  bool VerifyCommitted(Read read) const {
    return phase_ == Phase::Committed && Matches(read, image_.projected);
  }
  Phase phase() const { return phase_; }
  const InitializerCpuImage& image() const { return image_; }

 private:
  using Words = std::array<std::array<uint8_t, 4>, kInitializerCpuWords>;
  template <typename Read>
  static bool Matches(Read read, const Words& expected) {
    const auto fields = InitializerCpuLayout();
    for (size_t i = 0; i < fields.size(); ++i) {
      std::array<uint8_t, 4> bytes{};
      if (!read(fields[i].offset, bytes) || bytes != expected[i]) return false;
    }
    return true;
  }
  bool Poison() { phase_ = Phase::Poisoned; return false; }
  Phase phase_ = Phase::Empty;
  InitializerCpuImage image_{};
};
}  // namespace gpu
