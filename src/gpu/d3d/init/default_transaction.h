#pragma once

#include "gpu/d3d/init/default_bootstrap.h"
#include <algorithm>
#include <span>
#include <vector>

namespace gpu {
struct DefaultCpuRange { uint32_t offset, bytes; };

// CPU selection of the accepted complete-input observation (2239 bytes).
// The inherited stack byte has a separate owner and is never written here.
constexpr bool DefaultTransactionNeeds(unsigned at) {
  if (DefaultBootstrapWrites(at, true)) return true;
  constexpr std::array<DefaultCpuRange, 11> inputs{{
      {48,12}, {1152,624}, {10943,1}, {12187,1}, {12440,20},
      {12648,16}, {12676,16}, {13784,8}, {14304,8}, {12255,1}, {14816,8}}};
  for (auto r : inputs) if (at >= r.offset && at - r.offset < r.bytes) return true;
  return false;
}

// Host memory mechanics and retained plan only; no live adapter or ABI.
// All calls require external exclusive access. Authority must prove the same
// generation/device/session, current metadata, retained owners, stack byte,
// and absence of concurrent consumers of these bytes. Sequential comparisons
// do not provide an atomic snapshot or replace that concurrency obligation.
class DefaultCpuTransaction final {
 public:
  enum class Phase { Empty, Capturing, Captured, Committing, Committed, Verifying,
                     Restoring, Restored, Releasing, Released, Poisoned };
  DefaultCpuTransaction() = default;
  DefaultCpuTransaction(const DefaultCpuTransaction&) = delete;
  DefaultCpuTransaction& operator=(const DefaultCpuTransaction&) = delete;

  template <typename Authority, typename Read>
  bool Capture(uint64_t generation, const DefaultBootstrapInput& input,
               Authority authority, Read read) {
    if (phase_ != Phase::Empty) return Poison();
    phase_ = Phase::Capturing; // Exclude accessor/authority reentry as well.
    if (!generation || !input.stack91 || !input.device_address ||
        uint64_t(input.device_address) + 65536 > (uint64_t(1) << 32)) return Poison();
    generation_ = generation;
    input_ = input;
    reads_ = Ranges([](unsigned at) { return DefaultTransactionNeeds(at); });
    if (!Authorized(Phase::Capturing, authority) ||
        !Matches(Phase::Capturing, read, input_.cpu) ||
        !Matches(Phase::Capturing, read, input_.cpu) ||
        !Authorized(Phase::Capturing, authority) ||
        !services_.Initialize(generation_) || !services_.Prepare(generation_, input_, plan_)) return Poison();
    writes_ = Ranges([&](unsigned at) { return DefaultBootstrapWrites(at, plan_.direct.call31DD8); });
    if (!Authorized(Phase::Capturing, authority)) return Poison();
    phase_ = Phase::Captured;
    return true;
  }

  template <typename Authority, typename Read, typename Write>
  bool Commit(uint64_t generation, Authority authority, Read read, Write write) {
    if (phase_ != Phase::Captured || generation != generation_) return Poison();
    phase_ = Phase::Committing;
    if (!Authorized(Phase::Committing, authority) ||
        !Matches(Phase::Committing, read, input_.cpu)) return Poison();
    for (auto r : writes_) {
      if (!Authorized(Phase::Committing, authority) ||
          !write(r.offset, std::span<const uint8_t>(plan_.cpu.cpu.data() + r.offset, r.bytes)) ||
          phase_ != Phase::Committing) return Poison();
    }
    if (!Matches(Phase::Committing, read, plan_.cpu) ||
        !Authorized(Phase::Committing, authority)) return Poison();
    phase_ = Phase::Committed;
    return true;
  }

  template <typename Authority, typename Read>
  bool VerifyCommitted(uint64_t generation, Authority authority, Read read) {
    if (phase_ != Phase::Committed || generation != generation_) return Poison();
    phase_ = Phase::Verifying;
    if (!Authorized(Phase::Verifying, authority) ||
        !Matches(Phase::Verifying, read, plan_.cpu) ||
        !Authorized(Phase::Verifying, authority)) return Poison();
    phase_ = Phase::Committed;
    return true;
  }

  // joined must prove both private joins of this session. Restore this layer
  // BEFORE verifying/restoring video, then initializer: before-images overlap.
  template <typename Joined, typename Read, typename Write>
  bool RestoreAfterJoins(uint64_t generation, Joined joined, Read read, Write write) {
    if (phase_ != Phase::Committed || generation != generation_) return Poison();
    phase_ = Phase::Restoring;
    if (!Authorized(Phase::Restoring, joined) ||
        !Matches(Phase::Restoring, read, plan_.cpu)) return Poison();
    for (auto i = writes_.rbegin(); i != writes_.rend(); ++i) {
      if (!Authorized(Phase::Restoring, joined) ||
          !write(i->offset, std::span<const uint8_t>(input_.cpu.cpu.data() + i->offset, i->bytes)) ||
          phase_ != Phase::Restoring) return Poison();
    }
    if (!Matches(Phase::Restoring, read, input_.cpu) ||
        !Authorized(Phase::Restoring, joined)) return Poison();
    phase_ = Phase::Restored;
    return true;
  }

  template <typename Joined>
  bool ReleaseAfterRestore(uint64_t generation, Joined joined) {
    if (phase_ != Phase::Restored || generation != generation_) return Poison();
    phase_ = Phase::Releasing;
    if (!Authorized(Phase::Releasing, joined) || !services_.Release(generation_)) return Poison();
    phase_ = Phase::Released;
    return true;
  }
  Phase phase() const { return phase_; }
  uint64_t generation() const { return generation_; }
  bool services_retained() const { return services_.active(); }
  bool CanConsume() const { return false; }
  const DefaultBootstrapInput& input() const { return input_; }
  const DefaultBootstrapPlan& plan() const { return plan_; }
  const std::vector<DefaultCpuRange>& read_ranges() const { return reads_; }
  const std::vector<DefaultCpuRange>& write_ranges() const { return writes_; }

 private:
  template <typename Predicate>
  static std::vector<DefaultCpuRange> Ranges(Predicate needs) {
    std::vector<DefaultCpuRange> ranges;
    for (unsigned at = 0; at < 65536;) {
      if (!needs(at)) { ++at; continue; }
      const unsigned begin = at;
      while (at < 65536 && needs(at)) ++at;
      ranges.push_back({begin, at - begin});
    }
    return ranges;
  }
  template <typename Authority>
  bool Authorized(Phase phase, Authority authority) const {
    return phase_ == phase && authority() && phase_ == phase;
  }
  template <typename Read>
  bool Matches(Phase phase, Read read, const ScalarDefaultInput& expected) const {
    for (auto r : reads_) {
      std::vector<uint8_t> bytes(r.bytes);
      if (phase_ != phase || !read(r.offset, std::span<uint8_t>(bytes)) || phase_ != phase ||
          !std::equal(bytes.begin(), bytes.end(), expected.cpu.begin() + r.offset)) return false;
    }
    return phase_ == phase;
  }
  bool Poison() { phase_ = Phase::Poisoned; return false; }
  Phase phase_ = Phase::Empty;
  uint64_t generation_ = 0;
  DefaultBootstrapServices services_{};
  DefaultBootstrapInput input_{};
  DefaultBootstrapPlan plan_{};
  std::vector<DefaultCpuRange> reads_, writes_;
};
} // namespace gpu
