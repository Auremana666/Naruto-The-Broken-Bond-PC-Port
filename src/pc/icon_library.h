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

// Each rewritten icon holds both looks: the original button (instance "o") and a keycap ("k")
// with up to kSlots letters on it ("g0".."g7", instances of one sprite with a frame per
// character). A script on the icon's first frame (the icon loops over two frames) sets their
// visibility, frames, positions and scales from numbers written in its own bytecode, so the
// port can switch the look and the key while the game runs (IconValues, then write each value
// where FindValues found it). Every number is pushed as "push marker; pop; push value": the
// marker (kMarker | id) locates the value in the game's copy of the bytecode.
constexpr int kSlots = 8;
constexpr int kFields = 2 + kSlots * 5;  // o visible, k visible, then per slot frame x y scale visible
constexpr uint32_t kMarker = 0x7E5E0000u;

struct Rect {
  int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};

struct Icon {
  std::string name;      // without the "mcExternalGenericIcons_" prefix
  Rect label_box;        // where the key label goes, in the icon's twips
  uint16_t first_id = 0;  // marker id of field 0; field f has first_id + f
};

struct Result {
  std::vector<uint8_t> movie;  // the rewritten movie (uncompressed GFX)
  int icons = 0;               // icons that became keycaps
  std::vector<Icon> list;
  std::string error;           // why nothing was rewritten
};

// font: a TrueType font (the menu's Titan One) used for the key labels. keyboard: the look the
// icons start with (their values before the port writes any).
Result RewriteIconLibrary(const uint8_t* data, size_t size, const LabelLookup& labels,
                          const uint8_t* font, size_t font_size, bool keyboard);

// The values of one icon's fields for a label (keyboard) or the original look.
std::vector<int32_t> IconValues(const Icon& icon, const std::string& label, bool keyboard,
                                const uint8_t* font);

}  // namespace icon_library
