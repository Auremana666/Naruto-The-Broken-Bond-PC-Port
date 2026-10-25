// Keyboard keys on the game's button icons: rewrites the icon library movie.
//
// Every interface movie imports its button icons (mcExternalGenericIcons_ButtonA, _ButtonLB,
// _Start, _DPadUp, ...) from one library, _ExternalGenericIcons.gfx. RewriteIconLibrary takes
// that file as the game stores it (a CFX, a zlib-compressed Scaleform movie) and returns an
// uncompressed GFX where each still button icon is a keycap showing the bound key. Animated
// icons and everything else in the library are kept as they are.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace icon_library {

// Key label for one icon, from the export name without the "mcExternalGenericIcons_" prefix
// ("ButtonA", "ButtonA_Grey", "DPadUp", ...). Empty: keep the original icon.
using LabelLookup = std::function<std::string(std::string_view icon)>;

struct Result {
  std::vector<uint8_t> movie;  // the rewritten movie (uncompressed GFX)
  int icons = 0;               // icons that became keycaps
  std::string error;           // why nothing was rewritten
};

// font: a TrueType font (the menu's Titan One) used for the key labels.
Result RewriteIconLibrary(const uint8_t* data, size_t size, const LabelLookup& labels,
                          const uint8_t* font, size_t font_size);

}  // namespace icon_library
