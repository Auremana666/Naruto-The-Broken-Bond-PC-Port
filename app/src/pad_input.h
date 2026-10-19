// Reads Xbox controllers (XInput) for the PC options menu. The game's own controller input goes
// through the SDK; this is separate so the menu works while the game is not reading input.

#pragma once

#include <cstddef>
#include <cstdint>

// Controller buttons the menu reads, as bits.
enum PadBit : uint32_t {
  kPadUp = 1u << 0,
  kPadDown = 1u << 1,
  kPadLeft = 1u << 2,
  kPadRight = 1u << 3,
  kPadA = 1u << 4,
  kPadB = 1u << 5,
  kPadX = 1u << 6,
  kPadY = 1u << 7,
  kPadStart = 1u << 8,
  kPadBack = 1u << 9,
};
constexpr uint32_t kPadDirs = kPadUp | kPadDown | kPadLeft | kPadRight;

// Buttons held on any connected controller (left stick counts as the D-pad).
uint32_t ReadPads();

// Every button and trigger of all connected controllers combined (XInput button bits).
struct PadRaw {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
};
PadRaw ReadPadRaw();

// One controller (XInput slot 0-3). connected is false when nothing is plugged into that slot.
PadRaw ReadPadRawAt(int index, bool* connected);

// Whether a Ctrl key is held right now (for Ctrl + key shortcuts).
bool CtrlHeld();

// Data built into the executable (RCDATA resource id); false if it is missing.
bool ReadResource(int id, const void** data, size_t* size);

// A Windows error box titled with the game's name.
void ShowGameError(const char* text);
