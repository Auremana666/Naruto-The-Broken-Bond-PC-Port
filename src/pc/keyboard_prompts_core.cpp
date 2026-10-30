#include "keyboard_prompts_core.h"

#include <array>
#include <cctype>
#include <utility>

namespace keyboard_prompts {

namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

bool EqualsNoCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// Button tags that stand for one keybind.
constexpr std::array<std::pair<std::string_view, std::string_view>, 18> kSingle = {{
    {"<a>", "keybind_a"},
    {"<b>", "keybind_b"},
    {"<x>", "keybind_x"},
    {"<y>", "keybind_y"},
    {"<lb>", "keybind_left_shoulder"},
    {"<rb>", "keybind_right_shoulder"},
    {"<lt>", "keybind_left_trigger"},
    {"<rt>", "keybind_right_trigger"},
    {"<start>", "keybind_start"},
    {"<back>", "keybind_back"},
    {"<dpadup>", "keybind_dpad_up"},
    {"<dpaddown>", "keybind_dpad_down"},
    {"<dpadleft>", "keybind_dpad_left"},
    {"<dpadright>", "keybind_dpad_right"},
    {"<analogup>", "keybind_lstick_up"},
    {"<analogdown>", "keybind_lstick_down"},
    {"<analogleft>", "keybind_lstick_left"},
    {"<analogright>", "keybind_lstick_right"},
}};

// Combo tags: a stick direction held with a face button.
constexpr std::array<std::pair<std::string_view, std::pair<std::string_view, std::string_view>>, 4>
    kCombo = {{
        {"<x-left>", {"keybind_lstick_left", "keybind_x"}},
        {"<x-right>", {"keybind_lstick_right", "keybind_x"}},
        {"<x-up>", {"keybind_lstick_up", "keybind_x"}},
        {"<y-up>", {"keybind_lstick_up", "keybind_y"}},
    }};

constexpr std::array<std::string_view, 5> kStickTags = {"<leftanalog>", "<analog>",
                                                        "<rightanalog>", "<analogrotate>",
                                                        "<dpad>"};

std::string FirstKey(std::string_view keys) {
  const size_t comma = keys.find(',');
  return std::string(Trim(keys.substr(0, comma)));
}

std::string Joined(const KeybindLookup& lookup, std::initializer_list<std::string_view> cvars) {
  std::string out;
  for (std::string_view cvar : cvars) {
    const std::string key = FirstKey(lookup(cvar));
    if (key.empty()) continue;
    if (!out.empty()) out += ' ';
    out += KeyLabel(key);
  }
  return out;
}

}  // namespace

std::string KeyLabel(std::string_view key) {
  static constexpr std::pair<std::string_view, std::string_view> kNames[] = {
      {"Return", "Enter"},       {"LMB", "Left Click"},     {"RMB", "Right Click"},
      {"MMB", "Middle Click"},   {"Escape", "Esc"},         {"Back", "Backspace"},
      {"Prior", "Page Up"},      {"Next", "Page Down"},     {"Control", "Ctrl"},
      {"Menu", "Alt"},           {"Capital", "Caps Lock"},  {"Up", "Up Arrow"},
      {"Down", "Down Arrow"},    {"Left", "Left Arrow"},    {"Right", "Right Arrow"},
  };
  key = Trim(key);
  for (const auto& [name, label] : kNames) {
    if (EqualsNoCase(key, name)) return std::string(label);
  }
  std::string out(key);
  if (out.size() == 1) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

std::string BindingLabel(std::string_view keys) {
  const std::string key = FirstKey(keys);
  return "[" + (key.empty() ? std::string("?") : KeyLabel(key)) + "]";
}

bool IsButtonTag(std::string_view tag) {
  for (const auto& [t, _] : kSingle) {
    if (tag == t) return true;
  }
  for (const auto& [t, _] : kCombo) {
    if (tag == t) return true;
  }
  for (std::string_view t : kStickTags) {
    if (tag == t) return true;
  }
  return false;
}

std::string TagText(std::string_view tag, const KeybindLookup& lookup, bool mouse_look) {
  for (const auto& [t, cvar] : kSingle) {
    if (tag == t) return BindingLabel(lookup(cvar));
  }
  for (const auto& [t, cvars] : kCombo) {
    if (tag == t) return BindingLabel(lookup(cvars.first)) + "+" + BindingLabel(lookup(cvars.second));
  }
  if (tag == "<leftanalog>" || tag == "<analog>") {
    const std::string keys = Joined(lookup, {"keybind_lstick_up", "keybind_lstick_left",
                                             "keybind_lstick_down", "keybind_lstick_right"});
    return "[" + (keys.empty() ? std::string("?") : keys) + "]";
  }
  if (tag == "<analogrotate>") {
    const std::string keys = Joined(lookup, {"keybind_lstick_up", "keybind_lstick_left",
                                             "keybind_lstick_down", "keybind_lstick_right"});
    return "[Rotate " + (keys.empty() ? std::string("?") : keys) + "]";
  }
  if (tag == "<rightanalog>") {
    if (mouse_look) return "[Mouse]";
    const std::string keys = Joined(lookup, {"keybind_rstick_up", "keybind_rstick_left",
                                             "keybind_rstick_down", "keybind_rstick_right"});
    return "[" + (keys.empty() ? std::string("?") : keys) + "]";
  }
  if (tag == "<dpad>") {
    const std::string keys = Joined(lookup, {"keybind_dpad_up", "keybind_dpad_down",
                                             "keybind_dpad_left", "keybind_dpad_right"});
    return "[" + (keys.empty() ? std::string("?") : keys) + "]";
  }
  return {};
}

std::string IconKeyName(std::string_view key) {
  static constexpr std::pair<std::string_view, std::string_view> kNames[] = {
      {"Return", "ENTER"}, {"Escape", "ESC"},   {"Control", "CTRL"}, {"Menu", "ALT"},
      {"Back", "BKSP"},    {"Capital", "CAPS"}, {"Prior", "PGUP"},   {"Next", "PGDN"},
      {"Delete", "DEL"},   {"Insert", "INS"},   {"Up", "UP"},        {"Down", "DOWN"},
      {"Left", "LEFT"},    {"Right", "RIGHT"},
  };
  key = Trim(key);
  for (const auto& [name, label] : kNames) {
    if (EqualsNoCase(key, name)) return std::string(label);
  }
  std::string out(key);
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (out.rfind("NUMPAD", 0) == 0) out = "NUM" + out.substr(6);
  return out;
}

std::string IconLabel(std::string_view icon, const KeybindLookup& lookup, bool mouse_look) {
  // The base name: "ButtonA_64_Grey" -> "ButtonA", "ButtonLT_Hold" -> "ButtonLT".
  const std::string_view base = icon.substr(0, icon.find('_'));
  auto key = [&](std::string_view cvar) {
    const std::string k = FirstKey(lookup(cvar));
    return k.empty() ? std::string("?") : IconKeyName(k);
  };
  auto keys = [&](std::initializer_list<std::string_view> cvars) {
    std::string out;
    for (std::string_view cvar : cvars) {
      const std::string k = FirstKey(lookup(cvar));
      if (k.empty()) continue;
      const std::string name = IconKeyName(k);
      if (name.size() > 1) return std::string();  // only short keys fit together on one cap
      out += name;
    }
    return out;
  };
  static constexpr std::pair<std::string_view, std::string_view> kSingle[] = {
      {"ButtonA", "keybind_a"},
      {"ButtonB", "keybind_b"},
      {"ButtonX", "keybind_x"},
      {"ButtonY", "keybind_y"},
      {"ButtonLB", "keybind_left_shoulder"},
      {"ButtonRB", "keybind_right_shoulder"},
      {"ButtonLT", "keybind_left_trigger"},
      {"ButtonRT", "keybind_right_trigger"},
      {"Start", "keybind_start"},
      {"Back", "keybind_back"},
      {"DPadUp", "keybind_dpad_up"},
      {"DPadDown", "keybind_dpad_down"},
      {"DPadLeft", "keybind_dpad_left"},
      {"DPadRight", "keybind_dpad_right"},
      {"AnalogUp", "keybind_lstick_up"},
      {"AnalogDown", "keybind_lstick_down"},
      {"AnalogLeft", "keybind_lstick_left"},
      {"AnalogRight", "keybind_lstick_right"},
  };
  for (const auto& [name, cvar] : kSingle) {
    if (base == name) return key(cvar);
  }
  if (base == "DPad") {
    return keys({"keybind_dpad_up", "keybind_dpad_down", "keybind_dpad_left",
                 "keybind_dpad_right"});
  }
  if (base == "LeftAnalog" || base == "Analog") {
    return keys({"keybind_lstick_up", "keybind_lstick_left", "keybind_lstick_down",
                 "keybind_lstick_right"});
  }
  if (base == "RightAnalog") {
    return mouse_look ? std::string("MOUSE")
                      : keys({"keybind_rstick_up", "keybind_rstick_left", "keybind_rstick_down",
                              "keybind_rstick_right"});
  }
  return {};
}

bool ExpandButtonTags(std::string_view text, const KeybindLookup& lookup, bool mouse_look,
                      std::string* out) {
  out->clear();
  bool changed = false;
  size_t i = 0;
  while (i < text.size()) {
    const size_t open = text.find('<', i);
    if (open == std::string_view::npos) break;
    const size_t close = text.find('>', open + 1);
    if (close == std::string_view::npos) break;
    const std::string_view tag = text.substr(open, close - open + 1);
    out->append(text.substr(i, open - i));
    if (IsButtonTag(tag)) {
      out->append(TagText(tag, lookup, mouse_look));
      changed = true;
    } else {
      out->append(tag);
    }
    i = close + 1;
  }
  out->append(text.substr(i));
  return changed;
}

}  // namespace keyboard_prompts
