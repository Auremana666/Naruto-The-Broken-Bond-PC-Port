// Where the game is (first start): when no disc image or game folder is next to the executable,
// a window asks for it. See game_location.cpp.

#pragma once

#include <filesystem>

// Asks where the game is and returns what the runtime mounts: a disc image or a folder with
// default.xex. Archives are unpacked first. Empty when the player cancels.
std::filesystem::path PickGameLocation();
