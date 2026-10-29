// Speed up: while the key of bind_speed_up (Numpad +) is held in the game's window, the game runs
// speed_up_factor times faster. The engine runs its fixed step once per frame, so the renderer's
// own limiter paces the frames that much faster (gpu::tick::SetSpeed); vertical sync or a slow
// graphics card caps it.

#include "pc/game_speed.h"

#include <windows.h>

#include <cstdlib>
#include <string>

#include <rex/cvar.h>
#include <rex/ui/keybinds.h>

#include "gpu/interpolation/tick.h"

REXCVAR_DEFINE_STRING(bind_speed_up, "NumpadPlus", "Keybinds", "Hold to speed the game up");
REXCVAR_DEFINE_STRING(speed_up_factor, "4", "Game",
                      "How fast the game runs while SPEED UP is held: 0.25, 0.5 or 0.75 (slow motion), 2 to 8");

namespace {
bool WindowFocused() {
  DWORD process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &process);
  return process == GetCurrentProcessId();
}
}  // namespace

void UpdateGameSpeed() {
  const auto key = rex::ui::ParseVirtualKey(REXCVAR_GET(bind_speed_up));
  const bool held = key != rex::ui::VirtualKey::kNone && WindowFocused() &&
                    (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
  const double factor = std::atof(REXCVAR_GET(speed_up_factor).c_str());
  gpu::tick::SetSpeed(held && factor >= 0.25 && factor <= 8.0 ? static_cast<float>(factor) : 1.0f);
}
