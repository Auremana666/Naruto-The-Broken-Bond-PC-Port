#include "key_bindings.h"

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ui/ui_event.h>

#include <iostream>
#include <stdexcept>

using rex::ui::VirtualKey;
using rex::input::mnk::MnkInputDriver;
using rex::X_RESULT;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void Set(const char* name, const std::string& value) {
  Require(rex::cvar::SetFlagByName(name, value), "Runtime rejected a launcher setting");
}

void Key(MnkInputDriver& driver, VirtualKey key, bool down) {
  rex::ui::KeyEvent event(nullptr, key, 1, false, false, false, false, false);
  if (down) driver.OnKeyDown(event);
  else driver.OnKeyUp(event);
}

rex::input::X_INPUT_STATE State(MnkInputDriver& driver) {
  rex::input::X_INPUT_STATE state{};
  Require(driver.GetDeviceState(static_cast<rex::input::DeviceId>(0x4D4E4B00), &state) ==
              X_ERROR_SUCCESS, "Keyboard device was not readable");
  return state;
}

int main() {
  try {
    Set("mnk_mode", "true");
    Set("mnk_mouse", "false");
    const auto defaults = DefaultKeyBinds();
    for (std::size_t i = 0; i < kKeyActionCount; ++i) Set(KeyActionOption(i), defaults[i]);
    MnkInputDriver driver(nullptr, 0);
    Key(driver, VirtualKey::kW, true);
    Key(driver, VirtualKey::kShift, true);
    Key(driver, VirtualKey::kControl, true);
    const auto held = State(driver);
    Require(held.gamepad.left_trigger == 255 && held.gamepad.right_trigger == 255,
            "Standalone Shift/Control did not activate both triggers");
    Require(held.gamepad.thumb_ly > 0, "Holding triggers suppressed movement");
    Key(driver, VirtualKey::kShift, false);
    Key(driver, VirtualKey::kControl, false);
    Require(State(driver).gamepad.left_trigger == 0 && State(driver).gamepad.right_trigger == 0,
            "Released triggers remained active");

    // Existing explicit modifier combinations still distinguish the D-pad from the stick.
    Set("keybind_right_trigger", "O");
    Set("keybind_dpad_up", "Shift+Up");
    Key(driver, VirtualKey::kUp, true);
    Require(State(driver).gamepad.thumb_ry > 0, "Unmodified arrow did not move the right stick");
    Key(driver, VirtualKey::kShift, true);
    const auto combo = State(driver);
    Require((combo.gamepad.buttons & rex::input::X_INPUT_GAMEPAD_DPAD_UP) != 0 &&
                combo.gamepad.thumb_ry == 0, "Explicit Shift+Up combination was not isolated");
    rex::ui::UISetupEvent lost;
    driver.OnLostFocus(lost);
    const auto unfocused = State(driver);
    Require(unfocused.gamepad.buttons == 0 && unfocused.gamepad.thumb_ly == 0,
            "Input was held after focus loss");
    std::cout << "Launcher bindings work in the SDK driver, including simultaneous modifiers and movement.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
