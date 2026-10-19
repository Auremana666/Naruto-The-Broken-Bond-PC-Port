#include "auto_jutsu.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "input_layout_core.h"
#include "player_assignment.h"

REXCVAR_DEFINE_BOOL(auto_jutsu, false, "Game",
                    "Perform a jutsu with one button (controller: hold LT + button, keyboard: one key)");
REXCVAR_DEFINE_STRING(auto_jutsu_key_1, "Numpad1", "Game", "Auto Jutsu: keyboard key for jutsu 1");
REXCVAR_DEFINE_STRING(auto_jutsu_key_2, "Numpad2", "Game", "Auto Jutsu: keyboard key for jutsu 2");
REXCVAR_DEFINE_STRING(auto_jutsu_key_3, "Numpad3", "Game", "Auto Jutsu: keyboard key for jutsu 3");
REXCVAR_DEFINE_STRING(auto_jutsu_key_4, "Numpad4", "Game", "Auto Jutsu: keyboard key for jutsu 4");
REXCVAR_DEFINE_STRING(auto_jutsu_pad1_1, "a", "Game", "Auto Jutsu: controller 1 button for jutsu 1");
REXCVAR_DEFINE_STRING(auto_jutsu_pad1_2, "b", "Game", "Auto Jutsu: controller 1 button for jutsu 2");
REXCVAR_DEFINE_STRING(auto_jutsu_pad1_3, "y", "Game", "Auto Jutsu: controller 1 button for jutsu 3");
REXCVAR_DEFINE_STRING(auto_jutsu_pad1_4, "x", "Game", "Auto Jutsu: controller 1 button for jutsu 4");
REXCVAR_DEFINE_STRING(auto_jutsu_pad2_1, "a", "Game", "Auto Jutsu: controller 2 button for jutsu 1");
REXCVAR_DEFINE_STRING(auto_jutsu_pad2_2, "b", "Game", "Auto Jutsu: controller 2 button for jutsu 2");
REXCVAR_DEFINE_STRING(auto_jutsu_pad2_3, "y", "Game", "Auto Jutsu: controller 2 button for jutsu 3");
REXCVAR_DEFINE_STRING(auto_jutsu_pad2_4, "x", "Game", "Auto Jutsu: controller 2 button for jutsu 4");
REXCVAR_DEFINE_BOOL(log_auto_jutsu, false, "Game", "Log Auto Jutsu activity (diagnostics)");
REXCVAR_DEFINE_BOOL(auto_jutsu_test_pad, false, "Game",
                    "Testing: F5 A, F6 left, F8 right, F9 LT, F10 Start, F11 down, F12 B on controllers");

namespace {

constexpr int kSlots = 4;
constexpr int kRotate = 99;

// Timeline of one jutsu, in seconds from the button press.
constexpr double kPrepWaitMin = 0.12;  // LT held before the first move when the button pressed it
constexpr double kPrepWaitPad = 0.04;  // the controller already holds LT
constexpr double kPrepWaitMax = 0.5;   // no fighter in the jutsu menu by then: give up
constexpr double kStepHold = 0.15;
constexpr double kStepGap = 0.10;
constexpr double kRotateTime = 0.6;
constexpr double kMaxCharge = 6.0;  // LT is let go after this even if the button stays held
constexpr uint8_t kTriggerDown = 30;

struct JutsuEntry {
  int level = 0;
  int first = 0;
  int second = 0;
};

// Where the game keeps its fighters, and what their data holds (guest addresses / offsets).
constexpr uint32_t kFighterLeft = 0x83368CA4;   // also what "1" refers to in the game's references
constexpr uint32_t kFighterRight = 0x83368CA8;  // "2"
constexpr uint32_t kThird = 0x833A3384;         // "3" in the jutsu table lookup
constexpr uint32_t kJutsuTable = 0x833B5DA0;
constexpr uint32_t kInJutsuMenu = 0x73C;   // fighter data: 1 while LT holds the jutsu menu open
constexpr uint32_t kJutsuOwner = 456;      // fighter data: who holds its jutsu ids
constexpr uint32_t kSlotState = 1516;      // fighter data: who holds each slot's availability
constexpr uint32_t kJutsuMode = 2084;      // fighter data: 0 normal, 1 full bar
constexpr uint32_t kJutsuIds = 491 * 4;    // owner data: jutsu id of each slot
constexpr uint32_t kSlotAvail = 71 * 4;    // slot state data: availability of each slot

std::mutex g_mutex;
uint8_t* g_base = nullptr;
int g_primary_user = -1;  // the player who pressed Start first: the game's P1

double Now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Whether each fighter (left, right) has its jutsu menu open, and the fighters themselves.
struct MenuState {
  uint32_t fighters[2] = {0, 0};
  bool open[2] = {false, false};
};

enum class Phase { kIdle, kWaitMenu, kMoves, kCharge, kRelease };

struct Runtime {
  Phase phase = Phase::kIdle;
  int slot = 0;
  double start = 0.0;
  double moves_start = 0.0;
  bool lt_was_down = false;  // the player already held LT when the jutsu button was pressed
  MenuState menus_at_start;  // which fighters had their jutsu menu open at the press
  JutsuEntry jutsu;
  std::array<bool, kSlots> was_held{};
  uint32_t last_raw_packet = 0;
  uint32_t packet_bias = 0;
  uint64_t last_out = 0;
};
std::unordered_map<uint64_t, Runtime> g_devices;

std::array<std::string, kSlots> KeyCvars() {
  return {REXCVAR_GET(auto_jutsu_key_1), REXCVAR_GET(auto_jutsu_key_2),
          REXCVAR_GET(auto_jutsu_key_3), REXCVAR_GET(auto_jutsu_key_4)};
}

// The jutsu buttons of controller n (0 or 1).
std::array<std::string, kSlots> PadCvars(int n) {
  if (n == 0) {
    return {REXCVAR_GET(auto_jutsu_pad1_1), REXCVAR_GET(auto_jutsu_pad1_2),
            REXCVAR_GET(auto_jutsu_pad1_3), REXCVAR_GET(auto_jutsu_pad1_4)};
  }
  if (n == 1) {
    return {REXCVAR_GET(auto_jutsu_pad2_1), REXCVAR_GET(auto_jutsu_pad2_2),
            REXCVAR_GET(auto_jutsu_pad2_3), REXCVAR_GET(auto_jutsu_pad2_4)};
  }
  return {};
}

int ButtonIndex(const std::string& name) {
  for (int i = 0; i < layout::kButtonCount; ++i) {
    if (name == layout::kNames[i]) return i;
  }
  return layout::kNone;
}

// Keys held in the game window, from its own key events (the same ones the game's keyboard reads).
class KeyTracker : public rex::ui::WindowInputListener, public rex::ui::WindowListener {
 public:
  bool Held(const std::string& name) const {
    if (name.empty()) return false;
    const auto vk = static_cast<size_t>(rex::ui::ParseVirtualKey(name));
    return vk != 0 && vk < down_.size() && down_[vk].load();
  }
  void OnKeyDown(rex::ui::KeyEvent& e) override { Set(e, true); }
  void OnKeyUp(rex::ui::KeyEvent& e) override { Set(e, false); }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    for (auto& key : down_) key.store(false);
  }

 private:
  void Set(const rex::ui::KeyEvent& e, bool down) {
    const auto vk = static_cast<size_t>(e.virtual_key());
    if (vk < down_.size()) down_[vk].store(down);
  }
  std::array<std::atomic<bool>, 256> down_{};
};
KeyTracker g_keys;

// Reads a guest word; false if the address isn't mapped (no fight running, stale pointer).
bool Load(uint32_t addr, uint32_t& value) {
  if (!g_base || addr < 0x10000) return false;
  MEMORY_BASIC_INFORMATION info{};
  constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  if (!VirtualQuery(g_base + addr, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & kReadable) == 0 || (info.Protect & PAGE_GUARD) != 0) {
    return false;
  }
  const uint8_t* p = g_base + addr;
  value = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  return true;
}

int LoadInt(uint32_t addr, int fallback) {
  uint32_t value;
  return Load(addr, value) ? static_cast<int32_t>(value) : fallback;
}

// An object's data block.
bool Data(uint32_t object, uint32_t& data) {
  uint32_t a, b;
  return object && Load(object + 32, a) && Load(a + 4, b) && Load(b + 48, data) && data != 0;
}

// The game's object references: 1 and 2 are the fighters, 3 a third object, else a pointer.
uint32_t Resolve(uint32_t value, uint32_t three) {
  uint32_t out = value;
  if (value == 1) Load(kFighterLeft, out);
  if (value == 2) Load(kFighterRight, out);
  if (value == 3) out = three;
  return out;
}

// The jutsu a fighter can do right now, in the order its jutsu menu lists them. Like the game's
// own list, it leaves out jutsu that aren't available, which changes when the bar is full.
std::vector<JutsuEntry> AvailableJutsu(uint32_t fighter) {
  std::vector<JutsuEntry> list;
  uint32_t data, owner_ref, owner_data, state_ref, state_data, third, table_ref, table_data;
  if (!Data(fighter, data) || !Load(data + kJutsuOwner, owner_ref) ||
      !Data(Resolve(owner_ref, fighter), owner_data) || !Load(data + kSlotState, state_ref) ||
      !Data(Resolve(state_ref, fighter), state_data) || !Load(kThird, third) ||
      !Load(kJutsuTable, table_ref) || !Data(Resolve(table_ref, third), table_data)) {
    return list;
  }
  const int mode = LoadInt(data + kJutsuMode, -1);
  if (mode < 0 || mode > 1) return list;
  for (int slot = 0; slot < kSlots; ++slot) {
    const int id = LoadInt(owner_data + kJutsuIds + slot * 4, -1);
    if (id < 0 || id > 98) continue;
    const int avail = LoadInt(state_data + kSlotAvail + slot * 4, -2);
    if (avail <= -2 || (mode == 1 && avail < 0)) continue;
    uint32_t jutsu_ref, jutsu_data;
    if (!Load(table_data + (id + 35) * 4, jutsu_ref)) continue;
    if (!Data(Resolve(jutsu_ref, third), jutsu_data)) continue;
    if (LoadInt(jutsu_data + (mode + 4) * 4, -1) == -1) continue;
    list.push_back({LoadInt(jutsu_data + 28, 0), LoadInt(jutsu_data + 36, 0),
                    LoadInt(jutsu_data + 40, 0)});
  }
  return list;
}

MenuState ReadMenus() {
  MenuState m;
  Load(kFighterLeft, m.fighters[0]);
  Load(kFighterRight, m.fighters[1]);
  for (int i = 0; i < 2; ++i) {
    uint32_t data;
    if (m.fighters[i] && Data(m.fighters[i], data)) m.open[i] = LoadInt(data + kInJutsuMenu, 0) == 1;
  }
  return m;
}

// The fighter whose jutsu menu this player opened: the one that opened it after the button was
// pressed, or the only one with it open. With both open, P1 is whoever pressed Start first and is
// taken to be on the left. Returns 0 while it can't tell yet.
uint32_t FighterInMenu(const MenuState& before, int user) {
  const MenuState now = ReadMenus();
  int newly = 0, open = 0, last_new = -1, last_open = -1;
  for (int i = 0; i < 2; ++i) {
    if (!now.open[i]) continue;
    ++open;
    last_open = i;
    if (!before.open[i]) {
      ++newly;
      last_new = i;
    }
  }
  if (REXCVAR_GET(log_auto_jutsu)) {
    REXLOG_INFO("[auto jutsu] menus left {:08X} {}->{} right {:08X} {}->{}, user {} first {}",
                now.fighters[0], before.open[0], now.open[0], now.fighters[1], before.open[1],
                now.open[1], user, g_primary_user);
  }
  if (newly == 1) return now.fighters[last_new];
  if (open == 1) return now.fighters[last_open];
  if (open == 2) {
    const bool first = g_primary_user < 0 ? user <= 0 : user == g_primary_user;
    return now.fighters[first ? 0 : 1];
  }
  return 0;
}

void Release(rex::input::X_INPUT_STATE& state, int button) {
  if (button == layout::kNone) return;
  if (button == layout::kLT) {
    state.gamepad.left_trigger = 0;
  } else if (button == layout::kRT) {
    state.gamepad.right_trigger = 0;
  } else {
    state.gamepad.buttons = static_cast<uint16_t>(state.gamepad.buttons & ~layout::kBits[button]);
  }
}

bool Pressed(const rex::input::X_INPUT_STATE& state, int button) {
  if (button == layout::kNone) return false;
  if (button == layout::kLT) return state.gamepad.left_trigger >= kTriggerDown;
  if (button == layout::kRT) return state.gamepad.right_trigger >= kTriggerDown;
  return (state.gamepad.buttons & layout::kBits[button]) != 0;
}

// Stick position for a direction code: up 1, right 2, down 3, left 4.
void Direction(int dir, int16_t& x, int16_t& y) {
  x = 0;
  y = 0;
  switch (dir) {
    case 1: y = 32767; break;
    case 2: x = 32767; break;
    case 3: y = -32767; break;
    case 4: x = -32767; break;
    default: break;
  }
}

void Sticks(rex::input::X_INPUT_STATE& state, int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
  state.gamepad.thumb_lx = lx;
  state.gamepad.thumb_ly = ly;
  state.gamepad.thumb_rx = rx;
  state.gamepad.thumb_ry = ry;
}

// Plays the moves of a jutsu; returns false once they are done.
bool PlayMoves(const JutsuEntry& jutsu, double t, rex::input::X_INPUT_STATE& state) {
  if (jutsu.first == kRotate) {
    if (t >= kRotateTime) {
      Sticks(state, 0, 0, 0, 0);
      return false;
    }
    // Two clockwise turns of the right stick, starting at the top.
    const double angle = t / kRotateTime * 2.0 * 6.283185307179586;
    Sticks(state, 0, 0, static_cast<int16_t>(std::sin(angle) * 32000.0),
           static_cast<int16_t>(std::cos(angle) * 32000.0));
    return true;
  }
  const int step = t < kStepHold                      ? 0
                   : t < kStepHold + kStepGap         ? -1
                   : t < 2 * kStepHold + kStepGap     ? 1
                   : t < 2 * kStepHold + 2 * kStepGap ? -1
                                                      : 2;
  if (step == 2) {
    Sticks(state, 0, 0, 0, 0);
    return false;
  }
  if (step < 0) {
    Sticks(state, 0, 0, 0, 0);
    return true;
  }
  const int code = step == 0 ? jutsu.first : jutsu.second;
  int16_t lx, ly, rx, ry;
  Direction((code - 1) / 4, lx, ly);
  Direction((code - 1) % 4 + 1, rx, ry);
  Sticks(state, lx, ly, rx, ry);
  return true;
}

bool Playable(const JutsuEntry& jutsu) {
  if (jutsu.first == kRotate) return true;
  return jutsu.first >= 5 && jutsu.first <= 20 && jutsu.second >= 5 && jutsu.second <= 20;
}

}  // namespace

void ApplyAutoJutsu(const rex::input::DeviceInfo& device, rex::input::X_INPUT_STATE& state) {
  const bool keyboard = device.synthetic && device.name == "Keyboard and Mouse";
  if (device.synthetic && !keyboard) return;
  const PlayerAssignment* assignment = PlayerAssignment::Current();
  const int user = assignment ? assignment->UserForDevice(device.id) : -1;
  const int controller = keyboard || !assignment ? -1 : assignment->ControllerNumber(device.id);
  const auto pad_cvars = PadCvars(controller);

  std::lock_guard lock(g_mutex);
  if (!keyboard && REXCVAR_GET(auto_jutsu_test_pad)) {
    // Testing without a second person: F-keys press this controller's buttons.
    static const std::array<std::pair<const char*, int>, 7> kTestKeys = {{
        {"F5", layout::kA}, {"F6", layout::kLeft}, {"F8", layout::kRight}, {"F9", layout::kLT},
        {"F10", layout::kStart}, {"F11", layout::kDown}, {"F12", layout::kB}}};
    for (const auto& [key, button] : kTestKeys) {
      if (!g_keys.Held(key)) continue;
      if (button == layout::kLT) {
        state.gamepad.left_trigger = 255;
      } else {
        state.gamepad.buttons = state.gamepad.buttons | layout::kBits[button];
      }
    }
  }
  // The game's P1 is whoever pressed Start first.
  if (g_primary_user < 0 && user >= 0 && Pressed(state, layout::kStart)) {
    g_primary_user = user;
  }
  if (!REXCVAR_GET(auto_jutsu)) {
    g_devices.clear();
    return;
  }
  Runtime& rt = g_devices[static_cast<uint64_t>(device.id)];
  const double now = Now();
  // LT as the player gives it (whatever button they have on LT, toggles included): it opens the
  // jutsu menu, and on a controller it is the button held while pressing a jutsu button.
  const bool lt_down = Pressed(state, layout::kLT);

  // Which jutsu buttons are pressed right now.
  std::array<bool, kSlots> held{};
  std::array<int, kSlots> pad_buttons{};
  const auto keys = KeyCvars();
  for (int i = 0; i < kSlots; ++i) {
    pad_buttons[i] = keyboard ? layout::kNone : ButtonIndex(pad_cvars[i]);
    if (pad_buttons[i] == layout::kLT) pad_buttons[i] = layout::kNone;
    if (keyboard) {
      held[i] = g_keys.Held(keys[i]);
    } else {
      held[i] = lt_down && Pressed(state, pad_buttons[i]);
    }
  }

  if (rt.phase == Phase::kIdle) {
    for (int i = 0; i < kSlots; ++i) {
      if (held[i] && !rt.was_held[i]) {
        rt.phase = Phase::kWaitMenu;
        rt.slot = i;
        rt.start = now;
        rt.lt_was_down = lt_down;
        rt.menus_at_start = ReadMenus();
        if (REXCVAR_GET(log_auto_jutsu)) {
          REXLOG_INFO("[auto jutsu] {} (player {}) jutsu {}", device.name, user + 1, i + 1);
        }
        break;
      }
    }
  }
  rt.was_held = held;

  // In the jutsu menu the controller's jutsu buttons belong to Auto Jutsu, not the game.
  if (!keyboard && (lt_down || rt.phase != Phase::kIdle)) {
    for (int b : pad_buttons) Release(state, b);
  }

  // The jutsu is charged while the player keeps LT or the jutsu button held.
  const bool button_held = lt_down || held[rt.slot];
  const double t = now - rt.start;
  switch (rt.phase) {
    case Phase::kIdle:
      break;
    case Phase::kWaitMenu: {
      state.gamepad.left_trigger = 255;
      Sticks(state, 0, 0, 0, 0);
      if (t < (rt.lt_was_down ? kPrepWaitPad : kPrepWaitMin)) break;
      const bool give_up = t >= kPrepWaitMax;
      const uint32_t fighter = FighterInMenu(rt.menus_at_start, user);
      if (!fighter) {
        if (give_up) rt.phase = Phase::kRelease;  // not in a fight
        break;
      }
      const auto list = AvailableJutsu(fighter);
      const JutsuEntry jutsu =
          rt.slot < static_cast<int>(list.size()) ? list[rt.slot] : JutsuEntry{};
      if (REXCVAR_GET(log_auto_jutsu)) {
        std::string text;
        for (const auto& j : list) text += fmt::format(" [{} {} {}]", j.level, j.first, j.second);
        REXLOG_INFO("[auto jutsu] fighter {:08X} jutsu{} -> using {}", fighter, text, rt.slot + 1);
      }
      if (Playable(jutsu)) {
        rt.jutsu = jutsu;
        rt.phase = Phase::kMoves;
        rt.moves_start = now;
      } else {
        rt.phase = Phase::kRelease;  // no jutsu in that place of the list
      }
      break;
    }
    case Phase::kMoves:
      state.gamepad.left_trigger = 255;
      if (!PlayMoves(rt.jutsu, now - rt.moves_start, state)) rt.phase = Phase::kCharge;
      break;
    case Phase::kCharge:
      // LT stays held while the player holds it or the jutsu button: letting go releases.
      if (button_held && t < kMaxCharge) {
        state.gamepad.left_trigger = 255;
        Sticks(state, 0, 0, 0, 0);
      } else {
        rt.phase = Phase::kRelease;
      }
      break;
    case Phase::kRelease:
      state.gamepad.left_trigger = 0;
      if (!button_held) rt.phase = Phase::kIdle;
      break;
  }

  // A changed state needs a new packet number, or the game ignores it.
  const uint32_t raw_packet = state.packet_number;
  const uint64_t out = static_cast<uint64_t>(static_cast<uint16_t>(state.gamepad.buttons)) |
                       (static_cast<uint64_t>(state.gamepad.left_trigger) << 16) |
                       (static_cast<uint64_t>(state.gamepad.right_trigger) << 24) |
                       (static_cast<uint64_t>(static_cast<uint16_t>(state.gamepad.thumb_rx)) << 32) |
                       (static_cast<uint64_t>(static_cast<uint16_t>(state.gamepad.thumb_ry)) << 48);
  const uint64_t sticks_l =
      (static_cast<uint64_t>(static_cast<uint16_t>(state.gamepad.thumb_lx)) << 16) |
      static_cast<uint16_t>(state.gamepad.thumb_ly);
  const uint64_t signature = out ^ (sticks_l * 0x9E3779B97F4A7C15ull);
  if (signature != rt.last_out && raw_packet == rt.last_raw_packet) ++rt.packet_bias;
  rt.last_raw_packet = raw_packet;
  rt.last_out = signature;
  state.packet_number = raw_packet + rt.packet_bias;
}

void InstallAutoJutsu(rex::ui::Window* window, uint8_t* guest_memory) {
  {
    std::lock_guard lock(g_mutex);
    g_base = guest_memory;
  }
  if (!window) return;
  // Above every other listener, so it sees each key before anything marks it handled.
  window->AddInputListener(&g_keys, 1000000);
  window->AddListener(&g_keys);
}
