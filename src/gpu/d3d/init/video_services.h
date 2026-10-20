#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>

namespace gpu {
// Own semantic display state retained by the restricted 3E8F0 controller.
// Resource creation, GPU receipts, guest callbacks and presentation are blocked.
using VideoParameters = std::array<uint8_t, 124>;
using VideoMode = std::array<uint8_t, 48>;
using DisplayParameters = std::array<uint8_t, 56>;
using DisplayNotification = std::array<uint8_t, 64>;
using GammaRamp = std::array<uint8_t, 1536>;
using GammaTable = std::array<uint16_t, 1024>;

inline uint32_t VideoWord(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline void VideoStoreWord(uint8_t* p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (24 - 8 * i));
}
inline void VideoStoreHalf(uint8_t* p, uint16_t value) {
  p[0] = uint8_t(value >> 8); p[1] = uint8_t(value);
}

// Explicit local profile, matching the admitted SDK mode policy. A future
// presenter must implement this transfer or close a different profile first.
inline constexpr uint32_t kVideoWidth = 1280, kVideoHeight = 720;
inline constexpr uint32_t kVideoRefreshBits = 0x42700000; // 60 Hz
inline constexpr uint32_t kVideoGammaType = 2; // TV / BT.709
inline constexpr float kVideoGammaPower = float(2.22222233);

inline GammaTable BuildVideoGammaTable() {
  GammaTable result{};
  // Exact constants from IDA bytes and the PPC rounding stages in 3DBD0(0).
  const double scale = double(std::bit_cast<float>(uint32_t(0x3A802008)));
  const double srgb_offset = double(std::bit_cast<float>(uint32_t(0x3D6147AE)));
  const double srgb_scale = double(std::bit_cast<float>(uint32_t(0x3F72A76F)));
  const double srgb_linear = double(std::bit_cast<float>(uint32_t(0x3D9E8391)));
  const double srgb_power = std::bit_cast<double>(uint64_t(0x4003333340000000));
  const double tv_scale = double(std::bit_cast<float>(uint32_t(0x3F8CAC08)));
  const double tv_offset = double(std::bit_cast<float>(uint32_t(0x3DCAC083)));
  const double tv_power = std::bit_cast<double>(uint64_t(0x3FDCCCCCC0000000));
  for (unsigned i = 0; i < result.size(); ++i) {
    double value = double(float(double(i) * scale));
    if (value <= std::bit_cast<double>(uint64_t(0x3FA4B5DCC63F1412))) {
      value = double(float(value * srgb_linear));
    } else {
      value = double(float(value + srgb_offset));
      value = double(float(value * srgb_scale));
      value = double(float(std::pow(value, srgb_power)));
    }
    if (value < std::bit_cast<double>(uint64_t(0x3F926E978D4FDF3B))) {
      value = double(float(value * 4.5));
    } else {
      value = double(float(std::pow(value, tv_power)));
      value = double(float(std::fma(value, tv_scale, -tv_offset)));
    }
    result[i] = uint16_t(std::clamp(int(std::fma(value, 1023.0, 0.5)), 0, 1023));
  }
  return result;
}

inline GammaRamp BuildVideoGammaRamp(const GammaTable& table) {
  GammaRamp result{};
  for (unsigned channel = 0; channel < 3; ++channel) {
    for (unsigned i = 0; i < 256; ++i) {
      // 3E8F0 creates a 10-bit ramp by unsigned division, then 3DE88
      // indexes the transformed table. This is not an i * 257 ramp.
      const unsigned index = i * 1023 / 255;
      VideoStoreHalf(result.data() + channel * 512 + i * 2, uint16_t(table[index] << 6));
    }
  }
  return result;
}

struct VideoPlan {
  VideoParameters parameters{}; // device +13544; word +64 is normalized
  std::array<uint8_t, 16> mode_fields{}; // +21544/+21548/+21552/+21556
  DisplayParameters display_cache{}; // +13724, unresolved input copy
  DisplayNotification notification{}; // resolved temporary payload
  GammaRamp gamma{}; // +15004 only after a future authorized transaction
  uint32_t gamma_flags = 0, final_dirty = 0xFFFFFFFF;
  uint8_t worker_flags = 0;
  bool gamma_changed = false;
};

inline DisplayNotification BuildVideoDisplayNotification() {
  DisplayNotification result{};
  for (unsigned i = 0; i < 4; ++i)
    VideoStoreHalf(result.data() + 2 * i, uint16_t(i % 2 ? kVideoHeight : kVideoWidth));
  VideoStoreWord(result.data() + 8 + 8, kVideoWidth);
  VideoStoreWord(result.data() + 8 + 12, kVideoHeight);
  VideoStoreWord(result.data() + 8 + 16, kVideoWidth);
  VideoStoreWord(result.data() + 8 + 20, kVideoHeight);
  return result;
}

class VideoServices final {
 public:
  bool Initialize(uint64_t generation) {
    if (generation_ || !generation || generation <= last_generation_ ||
        std::fegetround() != FE_TONEAREST) return false;
    mode_ = {};
    const std::array<uint32_t, 12> words = {kVideoWidth, kVideoHeight, 0, 1, 1,
        kVideoRefreshBits, 1, 0x4A, 1, 0, 0, 0};
    for (unsigned i = 0; i < words.size(); ++i) VideoStoreWord(mode_.data() + 4 * i, words[i]);
    table_ = BuildVideoGammaTable();
    ramp_ = BuildVideoGammaRamp(table_);
    generation_ = last_generation_ = generation;
    return true;
  }
  bool QueryMode(uint64_t generation, VideoMode& mode) const {
    if (!Matches(generation)) return false;
    mode = mode_; return true;
  }
  bool QueryFlags(uint64_t generation, uint32_t& flags) const {
    if (!Matches(generation)) return false;
    flags = (VideoWord(mode_.data() + 12) ? 1u : 0u) |
        (VideoWord(mode_.data()) >= 1024 ? 2u : 0u) |
        (VideoWord(mode_.data()) >= 1920 ? 4u : 0u);
    return true;
  }
  bool QueryGamma(uint64_t generation, uint32_t& type, float& power) const {
    if (!Matches(generation)) return false;
    type = kVideoGammaType; power = kVideoGammaPower; return true;
  }
  bool Prepare(uint64_t generation, const VideoParameters& input,
               const std::array<uint32_t, 3>& slots, uint32_t gamma_owner,
               uint32_t gamma_flags, uint8_t worker_flags, const GammaRamp& previous_gamma,
               VideoPlan& output) const {
    if (!Matches(generation) || VideoWord(input.data()) != kVideoWidth ||
        VideoWord(input.data() + 4) != kVideoHeight ||
        VideoWord(input.data() + 8) != 0x18280186 || VideoWord(input.data() + 16) ||
        VideoWord(input.data() + 36) || VideoWord(input.data() + 44) ||
        VideoWord(input.data() + 56) != 1 || VideoWord(input.data() + 60) != 1 ||
        VideoWord(input.data() + 64) || gamma_owner || gamma_flags || worker_flags != 4 ||
        std::any_of(slots.begin(), slots.end(), [](uint32_t slot) { return slot != 0; }) ||
        std::any_of(input.begin() + 96, input.end(), [](uint8_t b) { return b != 0; })) return false;
    VideoPlan plan{};
    plan.parameters = input;
    VideoStoreWord(plan.parameters.data() + 64, 0x28280106);
    const std::array<uint32_t, 4> fields = {kVideoWidth, kVideoHeight, kVideoWidth, 60};
    for (unsigned i = 0; i < fields.size(); ++i)
      VideoStoreWord(plan.mode_fields.data() + 4 * i, fields[i]);
    // The all-zero 56-byte device cache is distinct from the resolved
    // rectangle/scaling passed in the temporary notification by 43590.
    plan.notification = BuildVideoDisplayNotification();
    plan.worker_flags = uint8_t(worker_flags | 0x10); // Plan only; no unsynchronized guest RMW.
    plan.gamma = ramp_;
    plan.gamma_changed = previous_gamma != ramp_;
    plan.gamma_flags = plan.gamma_changed ? 0x80000000 : 0;
    output = plan;
    return true;
  }
  bool Notify(uint64_t generation, uint32_t kind, const DisplayNotification& payload) {
    if (!Matches(generation) || kind != 1 || notification_version_) return false;
    if (payload != BuildVideoDisplayNotification()) return false;
    notification_ = payload; notification_version_ = 1;
    return true; // Local semantic notification; no guest callback or presentation.
  }
  bool Release(uint64_t generation) {
    if (!Matches(generation)) return false;
    generation_ = 0; notification_version_ = 0;
    mode_ = {}; table_ = {}; ramp_ = {}; notification_ = {};
    return true; // Host owner only. Guest integration must require private joins.
  }
  bool Matches(uint64_t generation) const { return generation && generation_ == generation; }
  uint64_t notification_version() const { return notification_version_; }
  const GammaTable& table() const { return table_; }
  const GammaRamp& ramp() const { return ramp_; }
  bool CanPresent() const { return false; }
 private:
  uint64_t generation_ = 0, last_generation_ = 0, notification_version_ = 0;
  VideoMode mode_{};
  GammaTable table_{};
  GammaRamp ramp_{};
  DisplayNotification notification_{};
};
} // namespace gpu
