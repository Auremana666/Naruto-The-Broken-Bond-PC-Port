#pragma once

#include "gpu/d3d/init/video_services.h"
#include <span>
#include <vector>

namespace gpu {
struct VideoCpuRange { uint32_t offset, bytes; };
// +10942 belongs to the synchronized worker flag owner, never this writer.
inline constexpr std::array<VideoCpuRange, 6> kVideoCpuRanges{{
    {13544, 124}, {13724, 56}, {15004, 1536}, {21544, 16}, {23956, 4}, {16708, 4}}};
using VideoCpuBytes = std::array<std::vector<uint8_t>, 6>;

class VideoCpuTransaction final {
 public:
  enum class Phase { Empty, Captured, Planned, Committed, Restored, Poisoned };
  VideoCpuTransaction() = default;
  VideoCpuTransaction(const VideoCpuTransaction&) = delete;
  VideoCpuTransaction& operator=(const VideoCpuTransaction&) = delete;
  template <typename Authority, typename Read>
  bool Capture(Authority authority, Read read) {
    if (phase_ != Phase::Empty || !authority()) return Poison();
    phase_ = Phase::Poisoned;
    for (size_t i = 0; i < kVideoCpuRanges.size(); ++i) {
      before_[i].resize(kVideoCpuRanges[i].bytes);
      if (!read(kVideoCpuRanges[i].offset, std::span<uint8_t>(before_[i]))) return false;
    }
    if (!Matches(read, before_) || !authority()) return false;
    phase_ = Phase::Captured;
    return true;
  }
  bool Plan(const VideoPlan& plan) {
    if (phase_ != Phase::Captured) return Poison();
    projected_[0].assign(plan.parameters.begin(), plan.parameters.end());
    projected_[1].assign(plan.display_cache.begin(), plan.display_cache.end());
    projected_[2].assign(plan.gamma.begin(), plan.gamma.end());
    projected_[3].assign(plan.mode_fields.begin(), plan.mode_fields.end());
    projected_[4].resize(4); VideoStoreWord(projected_[4].data(), plan.gamma_flags);
    projected_[5].resize(4); VideoStoreWord(projected_[5].data(), plan.final_dirty);
    phase_ = Phase::Planned;
    return true;
  }
  template <typename Authority, typename Read, typename Write>
  bool Commit(Authority authority, Read read, Write write) {
    if (phase_ != Phase::Planned || !authority() || !Matches(read, before_)) return Poison();
    phase_ = Phase::Poisoned;
    for (size_t i = 0; i < kVideoCpuRanges.size(); ++i) {
      if (!authority() || !write(kVideoCpuRanges[i].offset, std::span<const uint8_t>(projected_[i]))) return false;
    }
    if (!Matches(read, projected_) || !authority()) return false;
    phase_ = Phase::Committed;
    return true;
  }
  template <typename Authority, typename Read, typename Write>
  bool RestoreAfterJoins(Authority joined, Read read, Write write) {
    if (phase_ != Phase::Committed || !joined() || !Matches(read, projected_)) return Poison();
    phase_ = Phase::Poisoned;
    for (size_t i = kVideoCpuRanges.size(); i > 0; --i) {
      if (!joined() || !write(kVideoCpuRanges[i - 1].offset, std::span<const uint8_t>(before_[i - 1]))) return false;
    }
    if (!Matches(read, before_) || !joined()) return false;
    phase_ = Phase::Restored;
    return true;
  }
  template <typename Read> bool VerifyCommitted(Read read) const {
    return phase_ == Phase::Committed && Matches(read, projected_);
  }
  Phase phase() const { return phase_; }
  const VideoCpuBytes& before() const { return before_; }
  const VideoCpuBytes& projected() const { return projected_; }
 private:
  template <typename Read> static bool Matches(Read read, const VideoCpuBytes& expected) {
    for (size_t i = 0; i < kVideoCpuRanges.size(); ++i) {
      std::vector<uint8_t> current(kVideoCpuRanges[i].bytes);
      if (expected[i].size() != current.size() ||
          !read(kVideoCpuRanges[i].offset, std::span<uint8_t>(current)) || current != expected[i]) return false;
    }
    return true;
  }
  bool Poison() { phase_ = Phase::Poisoned; return false; }
  Phase phase_ = Phase::Empty;
  VideoCpuBytes before_{}, projected_{};
};
} // namespace gpu
