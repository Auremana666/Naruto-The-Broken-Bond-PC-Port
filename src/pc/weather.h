#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

// Applies a WEATHER choice made while playing, from the game thread (called once per frame while
// the player walks around).
void UpdateWeather(PPCContext& ctx, uint8_t* base);
