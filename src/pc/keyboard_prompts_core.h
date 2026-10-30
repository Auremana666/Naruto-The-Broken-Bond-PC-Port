// Keyboard button prompts: the text part, kept free of the runtime so it can be tested alone.
//
// The game writes its button prompts as tags inside its text ("Press <a> to jump."). Its Flash
// interface (Classes.Text.SpecialEffect.GetSpecialTag) turns each tag into an Xbox button icon.
// With keyboard prompts on, the port rewrites the tags into the keys bound to those buttons
// before the interface sees them ("Press [Space] to jump.").

#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace keyboard_prompts {

// Returns the keys bound to a keybind cvar ("keybind_a" -> "Space" or "J,LMB"); empty if none.
using KeybindLookup = std::function<std::string(std::string_view cvar)>;

// Text shown for one key name as stored in the config ("Return" -> "Enter", "LMB" -> "Left
// Click"). Unknown names are returned as they are.
std::string KeyLabel(std::string_view key);

// Text shown for a whole binding ("J,LMB" -> "[J]"): the first key in brackets, or "[?]" when
// nothing is bound.
std::string BindingLabel(std::string_view keys);

// True when the tag (with its angle brackets, lower case) is a controller button or stick tag.
bool IsButtonTag(std::string_view tag);

// Text for one button tag ("<a>" -> "[Space]"); empty if the tag is not a button tag.
// mouse_look: the right stick is on the mouse, so "<rightanalog>" reads "[Mouse]".
std::string TagText(std::string_view tag, const KeybindLookup& lookup, bool mouse_look);

// Short key name for a keycap icon ("Return" -> "ENTER", "Control" -> "CTRL", "j" -> "J").
std::string IconKeyName(std::string_view key);

// Label for one of the game's button icons, by its name in the icon library without the
// "mcExternalGenericIcons_" prefix ("ButtonA", "ButtonLB_64", "Start", "DPadUp", "AnalogLeft").
// Empty for icons that stay as they are (animations, stick rotations, non-button icons).
std::string IconLabel(std::string_view icon, const KeybindLookup& lookup, bool mouse_look);

// Replaces every button tag in the text; other tags (colours, <br>, <@...>) are kept.
// Returns true when anything changed.
bool ExpandButtonTags(std::string_view text, const KeybindLookup& lookup, bool mouse_look,
                      std::string* out);

}  // namespace keyboard_prompts
