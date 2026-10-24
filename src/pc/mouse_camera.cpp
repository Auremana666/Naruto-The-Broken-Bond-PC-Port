// Mouse camera (PC Options > Controls > Mouse Moves Camera): the mouse turns the free roaming
// camera by the distance it moved, instead of pushing the right stick.
//
// The game's camera input (828C54E8, once per logic step for the player) reads the right stick
// into the player's data: +964 its X, +184 the camera pitch (0 to the limit at +504, turned at a
// fixed speed whatever the push), +16 / +172 the game's invert flags for X / Y. The camera
// (82A47B68) then turns its direction around the vertical by an angle from +964 that ramps up
// while the stick is held, and only past a quarter push (82808FD8 turns the vector; called a
// second time with the opposite angle when the turn ran into a wall). Played as a stick, the
// mouse is slow, ramps and stops dead under a quarter push.
//
// Here, after the game's camera input, the mouse's vertical motion goes straight to the pitch,
// and its horizontal motion becomes the angle of the camera's turn: +964 is set to a quarter push
// so the camera turns at all, and the angle passed to 82808FD8 is replaced by the mouse's (added
// to the stick's when a controller turns too). The stick is read only when the camera is the
// player's to turn (not in cutscenes, menus or fights); the mouse follows the same rule, and
// elsewhere goes back to moving the right stick.

#include "mouse_camera.h"
#include "weather.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>

#include "generated/default/broken_bond_init.h"
#include "gpu/common/guest_memory.h"

REXCVAR_DEFINE_BOOL(mouse_camera_direct, true, "Controls",
                    "The mouse turns the camera by the distance it moved (off: it pushes the "
                    "right stick like a controller)");

extern "C" void __imp__sub_828C54E8(PPCContext& __restrict, uint8_t*);  // camera input
extern "C" void __imp__sub_828793B0(PPCContext& __restrict, uint8_t*);  // read the right stick
extern "C" void __imp__sub_82808FD8(PPCContext& __restrict, uint8_t*);  // turn a vector

namespace {

constexpr uint32_t kStickReadReturn = 0x828C5810;  // 828C54E8's read of the right stick
constexpr uint32_t kTurnReturn = 0x82A47F74;       // the camera's turn
constexpr uint32_t kTurnBackReturn = 0x82A47FE4;   // its undo after hitting a wall
constexpr uint32_t kStickX = 964, kPitch = 184, kPitchMax = 504;
constexpr uint32_t kInvertX = 16, kInvertY = 172, kStickDisabled = 1560;
constexpr float kTurnThreshold = 0.25f;  // The camera turns past a quarter push (820AB72C).
constexpr float kRadiansPerPixel = 0.0025f;
constexpr auto kIdle = std::chrono::milliseconds(500);

// The player's data while the camera input runs, and whether it read the stick this step.
thread_local uint32_t t_player = 0;
thread_local bool t_stick_read = false;
std::atomic<int64_t> g_last_turnable{0};  // steady clock ticks of the last step that read it

// The mouse's turn for the camera's next turn (radians, in the game's sign), whether the stick
// was pushed too, and the angle the turn used (for its undo).
std::atomic<float> g_mouse_turn{0.0f};
std::atomic<bool> g_mouse_turn_pending{false}, g_stick_turns{false};
std::atomic<float> g_applied_turn{0.0f};

int64_t Now() { return std::chrono::steady_clock::now().time_since_epoch().count(); }

// The SDK's mouse settings (rexruntime.dll), read by name.
bool MouseOn() {
  const std::string value = rex::cvar::GetFlagByName("mnk_mouse");
  return value == "true" || value == "1";
}
double Sensitivity() {
  const double value = std::atof(rex::cvar::GetFlagByName("mnk_sensitivity").c_str());
  return value > 0.0 ? value : 1.0;
}

bool Wanted() { return REXCVAR_GET(mouse_camera_direct) && MouseOn(); }

void Turn(uint8_t* base, uint32_t player) {
  float dx = 0.0f, dy = 0.0f;
  if (!rex::input::mnk::TakeMouseLook(&dx, &dy)) return;
  const float scale = kRadiansPerPixel * static_cast<float>(std::clamp(Sensitivity(), 0.05, 20.0));
  const float sign_x = gpu::Get32(base, player + kInvertX) ? -1.0f : 1.0f;
  const float sign_y = gpu::Get32(base, player + kInvertY) ? -1.0f : 1.0f;

  // Down looks down like the stick pulled back: the pitch goes up.
  const float pitch_max = gpu::GetFloat(base, player + kPitchMax);
  const float pitch = gpu::GetFloat(base, player + kPitch) + dy * scale * sign_y;
  if (std::isfinite(pitch) && std::isfinite(pitch_max) && pitch_max > 0.0f) {
    gpu::PutFloat(base, player + kPitch, std::clamp(pitch, 0.0f, pitch_max));
  }

  // Right turns like the stick to the right; the camera's angle is the opposite of the push.
  if (dx == 0.0f) return;
  const float turn = -dx * scale * sign_x;
  const float stick = gpu::GetFloat(base, player + kStickX);
  const bool stick_turns = std::isfinite(stick) && std::fabs(stick) >= kTurnThreshold;
  if (!stick_turns) gpu::PutFloat(base, player + kStickX, turn < 0.0f ? kTurnThreshold : -kTurnThreshold);
  g_stick_turns = stick_turns;
  g_mouse_turn = g_mouse_turn_pending ? g_mouse_turn.load() + turn : turn;
  g_mouse_turn_pending = true;
}

}  // namespace

void UpdateMouseCamera() {
  const bool turnable = Now() - g_last_turnable.load(std::memory_order_relaxed) <
                        std::chrono::steady_clock::duration(kIdle).count();
  rex::input::mnk::SetDirectMouseLook(Wanted() && turnable);
}

REX_HOOK_RAW(sub_828793B0) {
  if (t_player && static_cast<uint32_t>(ctx.lr) == kStickReadReturn) t_stick_read = true;
  __imp__sub_828793B0(ctx, base);
}

REX_HOOK_RAW(sub_828C54E8) {
  UpdateWeather(ctx, base);
  // The player's data: [[[r3 + 32] + 4] + 48], as the function finds it.
  const uint32_t object = ctx.r3.u32;
  const uint32_t a = object ? gpu::Get32(base, object + 32) : 0;
  const uint32_t b = a ? gpu::Get32(base, a + 4) : 0;
  const uint32_t player = b ? gpu::Get32(base, b + 48) : 0;
  t_player = player;
  t_stick_read = false;
  __imp__sub_828C54E8(ctx, base);
  const bool turnable = player && t_stick_read && !gpu::Get32(base, player + kStickDisabled);
  t_player = 0;
  if (!turnable) return;
  g_last_turnable.store(Now(), std::memory_order_relaxed);
  if (Wanted()) Turn(base, player);
}

REX_HOOK_RAW(sub_82808FD8) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  if (lr == kTurnReturn) {
    // The mouse's angle replaces the quarter push's (added to a real push's).
    float applied = 0.0f;
    if (g_mouse_turn_pending.exchange(false)) {
      applied = static_cast<float>(g_stick_turns ? ctx.f1.f64 : 0.0) + g_mouse_turn.load();
      ctx.f1.f64 = applied;
    }
    g_applied_turn = applied;
  } else if (lr == kTurnBackReturn) {
    const float applied = g_applied_turn.exchange(0.0f);
    if (applied != 0.0f) ctx.f1.f64 = -applied;
  }
  __imp__sub_82808FD8(ctx, base);
}
