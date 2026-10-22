#include "input_layout.h"

#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>

#include "auto_jutsu.h"
#include "input_layout_core.h"
#include "player_assignment.h"

#define LAYOUT_CVARS(n)                                                                        \
  REXCVAR_DEFINE_STRING(pad_map_##n, #n, "Input/Controller", "What controller button " #n   \
                        " sends (a button name or none)");                                    \
  REXCVAR_DEFINE_STRING(pad_mode_##n, "normal", "Input/Controller",                          \
                        "Controller button " #n " press mode: normal, toggle or turbo");     \
  REXCVAR_DEFINE_STRING(key_mode_##n, "normal", "Input/Keyboard",                            \
                        "Press mode of the keys bound to " #n ": normal, toggle or turbo");

REXCVAR_DEFINE_BOOL(log_input_layout, false, "Input", "Log button changes after remapping (diagnostics)");

// Controller 2 has its own layout (controller 1 keeps the pad_ settings it always had).
#define PAD2_CVARS(n)                                                                            REXCVAR_DEFINE_STRING(pad2_map_##n, #n, "Input/Controller", "What controller 2 button " #n                         " sends (a button name or none)");                                      REXCVAR_DEFINE_STRING(pad2_mode_##n, "normal", "Input/Controller",                                                 "Controller 2 button " #n " press mode: normal, toggle or turbo");

LAYOUT_CVARS(a)
LAYOUT_CVARS(b)
LAYOUT_CVARS(x)
LAYOUT_CVARS(y)
LAYOUT_CVARS(lb)
LAYOUT_CVARS(rb)
LAYOUT_CVARS(lt)
LAYOUT_CVARS(rt)
LAYOUT_CVARS(ls)
LAYOUT_CVARS(rs)
LAYOUT_CVARS(back)
LAYOUT_CVARS(start)
LAYOUT_CVARS(up)
LAYOUT_CVARS(down)
LAYOUT_CVARS(left)
LAYOUT_CVARS(right)
PAD2_CVARS(a)
PAD2_CVARS(b)
PAD2_CVARS(x)
PAD2_CVARS(y)
PAD2_CVARS(lb)
PAD2_CVARS(rb)
PAD2_CVARS(lt)
PAD2_CVARS(rt)
PAD2_CVARS(ls)
PAD2_CVARS(rs)
PAD2_CVARS(back)
PAD2_CVARS(start)
PAD2_CVARS(up)
PAD2_CVARS(down)
PAD2_CVARS(left)
PAD2_CVARS(right)

namespace {

std::mutex g_mutex;
// Controller 1 and controller 2 (controllers 3 and 4 use controller 1's).
std::array<layout::Layout, 2> g_pad_layouts = {layout::Layout::Identity(), layout::Layout::Identity()};
layout::Layout g_key_layout = layout::Layout::Identity();
std::array<bool, 2> g_pad_identity = {true, true};
bool g_key_identity = true;

struct DeviceRuntime {
  layout::DeviceMemory memory;
  uint32_t last_raw_packet = 0;
  uint32_t packet_bias = 0;
  layout::PadState last_out;
};
std::unordered_map<uint64_t, DeviceRuntime> g_devices;

layout::Mode ParseMode(const std::string& text) {
  if (text == "toggle") return layout::Mode::kToggle;
  if (text == "turbo") return layout::Mode::kTurbo;
  return layout::Mode::kNormal;
}

int ParseButton(const std::string& text, int fallback) {
  if (text == "none") return layout::kNone;
  for (int i = 0; i < layout::kButtonCount; ++i) {
    if (text == layout::kNames[i]) return i;
  }
  return fallback;
}

bool IsIdentity(const layout::Layout& l) {
  for (int i = 0; i < layout::kButtonCount; ++i) {
    if (l.buttons[i].target != i || l.buttons[i].mode != layout::Mode::kNormal) return false;
  }
  return true;
}

double Now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void ApplyLayout(const rex::input::DeviceInfo& device, rex::input::X_INPUT_STATE& state) {
  const bool keyboard = device.synthetic && device.name == "Keyboard and Mouse";
  if (device.synthetic && !keyboard) {
    return;  // the NOP stand-in
  }
  const PlayerAssignment* assignment = PlayerAssignment::Current();
  const int pad = keyboard || !assignment || assignment->ControllerNumber(device.id) != 1 ? 0 : 1;
  std::lock_guard lock(g_mutex);
  if (keyboard ? g_key_identity : g_pad_identity[pad]) {
    return;
  }
  auto& runtime = g_devices[static_cast<uint64_t>(device.id)];

  layout::PadState in;
  in.buttons = state.gamepad.buttons;
  in.left_trigger = state.gamepad.left_trigger;
  in.right_trigger = state.gamepad.right_trigger;
  const layout::PadState out =
      layout::Apply(keyboard ? g_key_layout : g_pad_layouts[pad], runtime.memory, Now(), in);

  // XInput reports a new packet number when the state changes. A toggle or turbo changes the
  // output without the physical input changing, so advance the number for the game.
  const uint32_t raw_packet = state.packet_number;
  const bool out_changed = out.buttons != runtime.last_out.buttons ||
                           out.left_trigger != runtime.last_out.left_trigger ||
                           out.right_trigger != runtime.last_out.right_trigger;
  if (out_changed && raw_packet == runtime.last_raw_packet) {
    ++runtime.packet_bias;
  }
  if (out_changed && REXCVAR_GET(log_input_layout)) {
    REXLOG_INFO("[layout] {} buttons {:04X} lt {} rt {}", device.name, out.buttons,
                out.left_trigger, out.right_trigger);
  }
  runtime.last_raw_packet = raw_packet;
  runtime.last_out = out;

  state.packet_number = raw_packet + runtime.packet_bias;
  state.gamepad.buttons = out.buttons;
  state.gamepad.left_trigger = out.left_trigger;
  state.gamepad.right_trigger = out.right_trigger;
}

void Filter(const rex::input::DeviceInfo& device, rex::input::X_INPUT_STATE& state) {
  ApplyLayout(device, state);
  ApplyAutoJutsu(device, state);  // after the remap, so its buttons are the ones the game sees
}

}  // namespace

void ReloadInputLayout() {
  std::array<layout::Layout, 2> pads;
  layout::Layout key = layout::Layout::Identity();
  for (int i = 0; i < layout::kButtonCount; ++i) {
    const std::string name = layout::kNames[i];
    for (int p = 0; p < 2; ++p) {
      const std::string prefix = p == 0 ? "pad_" : "pad2_";
      pads[p].buttons[i].target = ParseButton(rex::cvar::GetFlagByName(prefix + "map_" + name), i);
      pads[p].buttons[i].mode = ParseMode(rex::cvar::GetFlagByName(prefix + "mode_" + name));
    }
    key.buttons[i].mode = ParseMode(rex::cvar::GetFlagByName("key_mode_" + name));
  }
  std::lock_guard lock(g_mutex);
  g_pad_layouts = pads;
  g_key_layout = key;
  for (int p = 0; p < 2; ++p) g_pad_identity[p] = IsIdentity(pads[p]);
  g_key_identity = IsIdentity(key);
  // Drop latched toggles when the layout changes, so nothing stays held by surprise.
  g_devices.clear();
}

void InstallInputLayout(rex::input::InputSystem* input) {
  ReloadInputLayout();
  input->SetDeviceStateFilter(&Filter);
}
