#pragma once

#include <string>
#include <vector>

// The graphics cards reported by the selected API, using its device names:
// the values of the game's graphics_device option. Cached separately per API.
const std::vector<std::string>& GraphicsDevices(bool vulkan = false);
