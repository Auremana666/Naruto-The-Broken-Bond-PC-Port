// Free camera. While it is on, the engine's logic stops (gpu::tick::SetFrozen: only the view
// block runs, with a dt of 0), so the characters, the effects and the game's own camera stand
// still, and the display builds its view from our camera instead of the entry's.
//
// The entry's camera (the first 16 floats of the 172-byte view entries of world + 720) is an
// engine matrix: rows 0-2 the camera's axes in the world (row 0 points left, row 1 back, the
// camera looks along -row 1, row 2 up), row 3 its position. The world's up is +Z. The free camera starts where the game's camera is,
// and turns around the world's up (yaw) and its own right axis (pitch).
//
// Controls: W A S D move, Space / E up, Ctrl / Q down, Shift faster, the mouse (Mouse Moves
// Camera on) or the arrow keys look. A controller: left stick moves, right stick looks, right
// trigger up, left trigger down, A held faster.

#include "pc/free_camera.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>

#include "gpu/common/guest_memory.h"
#include "gpu/interpolation/tick.h"
#include "pc/pad_input.h"

namespace {
using Vec = std::array<float, 3>;

constexpr float kSpeed = 5.0f;          // Units per second (walking is about 7).
constexpr float kFastFactor = 4.0f;     // Shift or A held.
constexpr float kLookSpeed = 1.8f;      // Radians per second, arrow keys and the right stick.
constexpr float kRadiansPerPixel = 0.0025f;
constexpr float kPitchLimit = 1.45f;    // Radians from level.
constexpr uint16_t kPadButtonA = 0x1000;  // XInput's A.

std::atomic<bool> on{false};

// Game thread.
bool started = false;
Vec position{}, forward{};
float yaw = 0.0f, pitch = 0.0f;
std::chrono::steady_clock::time_point last;

bool WindowFocused() {
  DWORD process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &process);
  return process == GetCurrentProcessId();
}
bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
float Stick(int16_t value) {
  const float v = static_cast<float>(value) / 32767.0f;
  return std::fabs(v) < 0.24f ? 0.0f : std::clamp(v, -1.0f, 1.0f);
}
bool MouseOn() {
  const std::string value = rex::cvar::GetFlagByName("mnk_mouse");
  return value == "true" || value == "1";
}
double Sensitivity() {
  const double value = std::atof(rex::cvar::GetFlagByName("mnk_sensitivity").c_str());
  return value > 0.0 ? std::clamp(value, 0.05, 20.0) : 1.0;
}

// The camera's axes from yaw (around +Z, 0 looking along +Y) and pitch (up positive).
void Axes(Vec& right, Vec& ahead, Vec& up) {
  const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
  ahead = {-sy * cp, cy * cp, sp};
  right = {cy, sy, 0.0f};
  up = {sy * sp, -cy * sp, cp};
}

void Start(const std::array<float, 16>& m) {
  position = {m[12], m[13], m[14]};
  const Vec ahead{-m[4], -m[5], -m[6]};
  pitch = std::asin(std::clamp(ahead[2], -1.0f, 1.0f));
  yaw = std::atan2(-ahead[0], ahead[1]);
  last = std::chrono::steady_clock::now();
  started = true;
  REXLOG_INFO("[free camera] start at ({:.1f}, {:.1f}, {:.1f}) right ({:.2f} {:.2f} {:.2f}) forward ({:.2f} {:.2f} "
              "{:.2f}) up ({:.2f} {:.2f} {:.2f})",
              m[12], m[13], m[14], m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]);
}

void Move() {
  const auto now = std::chrono::steady_clock::now();
  const float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.1f);
  last = now;
  float move_x = 0, move_y = 0, move_z = 0, look_x = 0, look_y = 0;
  bool fast = false;
  if (WindowFocused()) {
    move_y += (Key('W') ? 1.0f : 0.0f) - (Key('S') ? 1.0f : 0.0f);
    move_x += (Key('D') ? 1.0f : 0.0f) - (Key('A') ? 1.0f : 0.0f);
    move_z += (Key(VK_SPACE) || Key('E') ? 1.0f : 0.0f) - (Key(VK_CONTROL) || Key('Q') ? 1.0f : 0.0f);
    look_x += (Key(VK_RIGHT) ? 1.0f : 0.0f) - (Key(VK_LEFT) ? 1.0f : 0.0f);
    look_y += (Key(VK_UP) ? 1.0f : 0.0f) - (Key(VK_DOWN) ? 1.0f : 0.0f);
    fast = Key(VK_SHIFT);
  }
  const PadRaw pad = ReadPadRaw();
  move_x += Stick(pad.left_x);
  move_y += Stick(pad.left_y);
  look_x += Stick(pad.right_x);
  look_y += Stick(pad.right_y);
  move_z += (pad.right_trigger - pad.left_trigger) / 255.0f;
  fast = fast || (pad.buttons & kPadButtonA);

  yaw -= look_x * kLookSpeed * dt;
  pitch += look_y * kLookSpeed * dt;
  float dx = 0.0f, dy = 0.0f;
  if (rex::input::mnk::TakeMouseLook(&dx, &dy)) {
    const float scale = kRadiansPerPixel * static_cast<float>(Sensitivity());
    yaw -= dx * scale;
    pitch -= dy * scale;
  }
  pitch = std::clamp(pitch, -kPitchLimit, kPitchLimit);

  Vec right, ahead, up;
  Axes(right, ahead, up);
  const float step = kSpeed * (fast ? kFastFactor : 1.0f) * dt;
  for (int i = 0; i < 3; ++i)
    position[i] += (right[i] * move_x + ahead[i] * move_y) * step + (i == 2 ? move_z * step : 0.0f);
}
}  // namespace

void ToggleFreeCamera() {
  const bool now_on = !on.load();
  on.store(now_on);
  gpu::tick::SetFrozen(now_on);
}

bool FreeCameraOn() { return on.load(std::memory_order_relaxed); }

bool ApplyFreeCamera(uint8_t* base, uint32_t entry, std::array<float, 16>& saved) {
  if (!FreeCameraOn()) {
    started = false;
    return false;
  }
  if (!entry) return false;
  for (uint32_t i = 0; i < 16; ++i) saved[i] = gpu::GetFloat(base, entry + 4 * i);
  for (float v : saved)
    if (!std::isfinite(v)) return false;
  if (!started) Start(saved);
  Move();
  Vec right, ahead, up;
  Axes(right, ahead, up);
  std::array<float, 16> m = saved;
  for (int i = 0; i < 3; ++i) {
    m[i] = -right[i];
    m[4 + i] = -ahead[i];
    m[8 + i] = up[i];
    m[12 + i] = position[i];
  }
  for (uint32_t i = 0; i < 16; ++i) gpu::PutFloat(base, entry + 4 * i, m[i]);
  return true;
}
