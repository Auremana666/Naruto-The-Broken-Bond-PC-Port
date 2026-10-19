// Auto Jutsu (PC Options > Cheats): one button performs a character's jutsu for you.
//
// Controller 1 and 2: hold LT (whatever button the player has on LT, which opens the jutsu
// menu) and press A, B, Y or X for jutsu 1-4. Keyboard: Numpad 1-4. All of them are rebindable in
// PC Options > Cheats > Auto Jutsu. The port then holds LT, plays that jutsu's stick moves, and
// keeps LT held for as long as LT or the button stays held, so the chakra charge is still yours
// to time. Jutsu N is the N-th jutsu the fighter's jutsu menu shows (a full bar changes it).
//
// The combos are not stored per character here: they are read from the fighter in the jutsu
// menu (its jutsu ids, then the game's jutsu table), the same data its jutsu menu shows.
// Combo codes: 99 = rotate the right stick; otherwise code = 4 * left + right, with stick
// directions up 1, right 2, down 3, left 4 (pushed at the same time). A jutsu is two such steps.

#pragma once

#include <cstdint>

#include <rex/input/device.h>
#include <rex/input/input.h>

namespace rex::ui {
class Window;
}

// Starts following the keyboard keys and reading the fighters. Call once the game window exists.
void InstallAutoJutsu(rex::ui::Window* window, uint8_t* guest_memory);

// Called from the input filter for every device state the game reads.
void ApplyAutoJutsu(const rex::input::DeviceInfo& device, rex::input::X_INPUT_STATE& state);
