#pragma once

#include <SDL3/SDL_events.h>

#include "pages/pages.h"

// The key mapping view of the settings: two keys for each controller input.
namespace launcher_ui {

// Draws the view in the given height.
void RenderKeyMapping(PageContext& context, float height);
// Puts the default layout back.
void RestoreDefaultKeys(PageContext& context);

// Sees each SDL event before ImGui. While a key is being captured, key and
// mouse button presses go to the capture and not to ImGui (Space would also
// press the focused button). Returns whether the event was taken.
bool CaptureKeyEvent(const SDL_Event& event);

}  // namespace launcher_ui
