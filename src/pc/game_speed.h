// Speed up (PC Options > Cheats > SPEED UP): while its key is held the whole game runs faster,
// for cutscenes that cannot be skipped. See game_speed.cpp.

#pragma once

// Reads the held key and sets the game speed. Cheap, so it is called every frame.
void UpdateGameSpeed();
