// Controller layout (any button can send any other, with Normal / Toggle / Turbo presses) and
// press modes for keyboard keys. Settings live in naruto.toml:
//   pad_map_<button>   what a controller button sends ("a", "rt", ..., or "none")
//   pad_mode_<button>  "normal", "toggle" or "turbo"
//   key_mode_<button>  press mode for the keyboard keys bound to that controller button
//   pad2_map_<button>, pad2_mode_<button>  the same for controller 2 (pad_ is controller 1)
// Button names: a b x y lb rb lt rt ls rs back start up down left right.

#pragma once

namespace rex::input {
class InputSystem;
}

// Installs the filter on the input system. Call before the game starts polling input.
void InstallInputLayout(rex::input::InputSystem* input);

// Re-reads the settings after the options menu changed one.
void ReloadInputLayout();
