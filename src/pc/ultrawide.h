// Ultrawide aspect ratios (PC Options > Display > Aspect Ratio). See ultrawide.cpp.

#pragma once

#include <cstdint>

// Applies the aspect_ratio setting to the game's camera and the presenter. Cheap when nothing
// changed, so it is called every frame.
void UpdateUltrawide(uint8_t* guest_memory);
