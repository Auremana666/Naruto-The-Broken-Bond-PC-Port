// Free camera (PC Options > Cheats > FREE CAMERA, key 6): the world stands still and the
// camera flies around it. See free_camera.cpp.

#pragma once

#include <array>
#include <cstdint>

// Turns the free camera on or off. Any thread.
void ToggleFreeCamera();
bool FreeCameraOn();

// The display's camera (82BD9108, r3 the view's entry), game thread: while the free camera is
// on, moves it by the input since the last call and writes it over the entry's camera matrix.
// Returns true when it wrote; saved then holds the entry's own matrix to put back after the view
// is built.
bool ApplyFreeCamera(uint8_t* base, uint32_t entry, std::array<float, 16>& saved);
