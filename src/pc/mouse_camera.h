// Mouse camera (PC Options > Controls > Mouse Moves Camera). See mouse_camera.cpp.

#pragma once

// Switches the mouse between turning the camera directly (while the player can turn it) and
// pushing the right stick. Cheap, so it is called every frame.
void UpdateMouseCamera();
