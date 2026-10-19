// Button remapping with Normal / Toggle / Turbo press modes, independent of the SDK so it can be
// tested on its own. input_layout.cpp applies it to the controller and keyboard states.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace layout {

// The 16 digital inputs of an Xbox 360 controller (triggers count as pressed past a threshold).
enum Button : int {
  kA, kB, kX, kY, kLB, kRB, kLT, kRT, kLS, kRS, kBack, kStart, kUp, kDown, kLeft, kRight,
  kButtonCount
};
constexpr int kNone = -1;  // a button mapped to nothing

// Short names used in naruto.toml (pad_map_a = "b").
constexpr std::array<const char*, kButtonCount> kNames = {
    "a", "b", "x", "y", "lb", "rb", "lt", "rt", "ls", "rs", "back", "start",
    "up", "down", "left", "right"};

// Bits in the XInput buttons word; triggers have none.
constexpr std::array<uint16_t, kButtonCount> kBits = {
    0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0, 0, 0x0040, 0x0080, 0x0020, 0x0010,
    0x0001, 0x0002, 0x0004, 0x0008};
constexpr uint16_t kMappableBits = 0xF3FF;  // every bit above (the Guide button passes through)

enum class Mode { kNormal, kToggle, kTurbo };

struct ButtonSetting {
  int target = kNone;
  Mode mode = Mode::kNormal;
};

struct Layout {
  std::array<ButtonSetting, kButtonCount> buttons{};
  // The identity layout: every button sends itself, normal presses.
  static Layout Identity() {
    Layout l;
    for (int i = 0; i < kButtonCount; ++i) l.buttons[i] = {i, Mode::kNormal};
    return l;
  }
};

struct PadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
};

// Per-device memory: previous presses (toggle edges) and latched toggles.
struct DeviceMemory {
  std::array<bool, kButtonCount> was_pressed{};
  std::array<bool, kButtonCount> latched{};
};

constexpr uint8_t kTriggerThreshold = 30;  // XInput's published threshold
constexpr double kTurboPeriod = 0.1;       // seconds: 50 ms pressed, 50 ms released

inline bool IsPressed(const PadState& s, int b) {
  if (b == kLT) return s.left_trigger >= kTriggerThreshold;
  if (b == kRT) return s.right_trigger >= kTriggerThreshold;
  return (s.buttons & kBits[b]) != 0;
}

// Applies the layout to one device's state. now is in seconds (for turbo).
inline PadState Apply(const Layout& layout, DeviceMemory& memory, double now, const PadState& in) {
  PadState out;
  out.buttons = in.buttons & ~kMappableBits;  // keep unmapped bits such as Guide
  const bool turbo_on = std::fmod(now, kTurboPeriod) < kTurboPeriod * 0.5;

  for (int b = 0; b < kButtonCount; ++b) {
    const bool pressed = IsPressed(in, b);
    const ButtonSetting& setting = layout.buttons[b];
    bool active = pressed;
    switch (setting.mode) {
      case Mode::kNormal:
        break;
      case Mode::kToggle:
        if (pressed && !memory.was_pressed[b]) memory.latched[b] = !memory.latched[b];
        active = memory.latched[b];
        break;
      case Mode::kTurbo:
        active = pressed && turbo_on;
        break;
    }
    memory.was_pressed[b] = pressed;
    if (!active || setting.target == kNone) continue;

    const int t = setting.target;
    if (t == kLT || t == kRT) {
      // A held trigger sent to a trigger keeps its analog depth; anything else pulls it fully.
      uint8_t value = 255;
      if (setting.mode == Mode::kNormal && (b == kLT || b == kRT)) {
        value = b == kLT ? in.left_trigger : in.right_trigger;
      }
      uint8_t& slot = t == kLT ? out.left_trigger : out.right_trigger;
      if (value > slot) slot = value;
    } else {
      out.buttons |= kBits[t];
    }
  }
  return out;
}

}  // namespace layout
