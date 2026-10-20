#include "pages/key_mapping.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

#include "key_bindings.h"
#include "key_names.h"
#include "localization.h"
#include "widgets.h"

namespace launcher_ui {
namespace {

// A capture gives up after this long without a key.
constexpr Uint64 kCaptureTimeoutMs = 5000;

struct Capture {
  bool active = false;
  std::size_t action = 0;
  std::size_t slot = 0;
  Uint64 started_ms = 0;
  std::string key;  // The key pressed, until the next frame applies it.
};

Capture g_capture;
// The last frame that drew the view; a capture ends when the view is left.
int g_drawn_frame = -1;

constexpr TextId kDirections[] = {TextId::DirectionUp, TextId::DirectionDown, TextId::DirectionLeft,
                                  TextId::DirectionRight};

// The stick or D-pad an action belongs to; Count for the buttons.
TextId GroupOf(std::size_t action) {
  if (action >= kDpadFirst) return TextId::DpadName;
  if (action >= kRightStickFirst) return TextId::RightStickName;
  if (action >= kLeftStickFirst) return TextId::LeftStickName;
  return TextId::Count;
}

// "LB", or "Left stick · Up".
std::string ActionLabel(std::size_t action) {
  if (action < kKeyButtonCount) return KeyButtonName(action);
  return std::string(Tr(GroupOf(action))) + " · " + Tr(kDirections[(action - kKeyButtonCount) % 4]);
}

// The key as the user's keyboard prints it: on an ABNT2 layout "Semicolon" is Ç.
std::string KeyDisplayName(std::string_view name) {
  if (name.empty()) return "—";
  if (name == "LMB") return Tr(TextId::MouseLeftName);
  if (name == "RMB") return Tr(TextId::MouseRightName);
  if (name == "MMB") return Tr(TextId::MouseMiddleName);
  if (name == "Space") return Tr(TextId::KeySpaceName);
  if (name == "Return") return "Enter";
  if (name == "Escape") return "Esc";
  if (name == "Shift") return "Shift";
  if (name == "Control") return "Ctrl";
  if (name == "Alt") return "Alt";
  if (name == "Up") return "↑";
  if (name == "Down") return "↓";
  if (name == "Left") return "←";
  if (name == "Right") return "→";
  if (name.starts_with("Numpad")) {
    std::string_view rest = name.substr(6);
    if (rest == "Star") rest = "*";
    else if (rest == "Plus") rest = "+";
    else if (rest == "Minus") rest = "-";
    else if (rest == "Slash") rest = "/";
    return "Num " + std::string(rest);
  }
  const SDL_Scancode scancode = ScancodeForKeyName(name);
  if (scancode != SDL_SCANCODE_UNKNOWN) {
    const char* shown = SDL_GetKeyName(SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false));
    if (shown && *shown) return shown;
  }
  // A value written by hand ("Shift+Up") shows as it is.
  return std::string(name);
}

std::string Format(TextId format, const std::string& a, const std::string& b = {}) {
  char text[256];
  std::snprintf(text, sizeof(text), Tr(format), a.c_str(), b.c_str());
  return text;
}

void SetKey(KeyBinds& binds, std::size_t action, std::size_t slot, const std::string& key) {
  auto keys = SplitKeyBind(binds[action]);
  keys[slot] = key;
  binds[action] = JoinKeyBind(keys[0], keys[1]);
}

// The captured key goes to its slot and leaves any other.
void Assign(PageContext& context, const std::string& key) {
  const std::size_t action = g_capture.action;
  if (IsReservedKey(key)) {
    context.status_message = Format(TextId::KeyReservedStatus, KeyDisplayName(key));
    return;
  }
  KeyBinds& binds = context.config.key_binds;
  std::string moved_from;
  for (std::size_t other = 0; other < kKeyActionCount; ++other) {
    auto keys = SplitKeyBind(binds[other]);
    bool changed = false;
    for (std::size_t slot = 0; slot < keys.size(); ++slot) {
      if (other == action && slot == g_capture.slot) {
        changed |= keys[slot] != key;
        keys[slot] = key;
      } else if (keys[slot] == key) {
        keys[slot].clear();
        changed = true;
        if (other != action) moved_from = ActionLabel(other);
      }
    }
    if (changed) binds[other] = JoinKeyBind(keys[0], keys[1]);
  }
  context.status_message = moved_from.empty() ? "" : Format(TextId::KeyMovedStatus, KeyDisplayName(key), moved_from);
}

void UpdateCapture(PageContext& context) {
  if (!g_capture.active) return;
  if (!g_capture.key.empty()) {
    Assign(context, g_capture.key);
    g_capture = {};
  } else if (SDL_GetTicks() - g_capture.started_ms > kCaptureTimeoutMs) {
    g_capture = {};
  }
}

struct Metrics {
  float label_width;
  float slot_width;
  float slot_gap;
  float row_height;
  float row_step;
  float heading_height;
  float section_gap;
};

void Slot(PageContext& context, std::size_t action, std::size_t slot, ImVec2 position, const Metrics& m) {
  const bool capturing = g_capture.active && g_capture.action == action && g_capture.slot == slot;
  const auto keys = SplitKeyBind(context.config.key_binds[action]);
  std::string text;
  if (capturing) {
    const Uint64 elapsed = SDL_GetTicks() - g_capture.started_ms;
    const int left = static_cast<int>((kCaptureTimeoutMs - std::min(elapsed, kCaptureTimeoutMs) + 999) / 1000);
    char prompt[64];
    std::snprintf(prompt, sizeof(prompt), Tr(TextId::KeyCapturePrompt), left);
    text = prompt;
  } else {
    text = KeyDisplayName(keys[slot]);
  }
  const bool empty = !capturing && keys[slot].empty();

  ImGui::PushID(static_cast<int>(action * 2 + slot));
  ImGui::SetCursorScreenPos(position);
  int colors = 0;
  if (capturing) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(42, 33, 25, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(42, 33, 25, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, kPrimary);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    colors = 4;
  } else if (empty) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    colors = 1;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, capturing ? 1.0f : 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, (m.row_height - ImGui::GetFontSize()) / 2));
  // A stable ID: the countdown changes the text every second.
  if (ImGui::Button((text + "###slot").c_str(), ImVec2(m.slot_width, m.row_height)) && !capturing) {
    g_capture = {true, action, slot, SDL_GetTicks(), {}};
  }
  const bool clear = !capturing && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(colors);
  ImGui::PopID();
  if (clear) SetKey(context.config.key_binds, action, slot, "");
}

void Row(PageContext& context, std::size_t action, const char* label, ImVec2 position, const Metrics& m) {
  ImGui::SetCursorScreenPos(ImVec2(position.x, position.y + (m.row_height - ImGui::GetFontSize()) / 2));
  ImGui::TextUnformatted(label);
  for (std::size_t slot = 0; slot < 2; ++slot) {
    const float x = position.x + m.label_width + slot * (m.slot_width + m.slot_gap);
    Slot(context, action, slot, ImVec2(x, position.y), m);
  }
}

void SectionHeading(const char* text, ImVec2 position) {
  ImGui::SetCursorScreenPos(position);
  PushBoldFont(ImGui::GetFontSize() - 2);
  ImGui::TextDisabled("%s", text);
  ImGui::PopFont();
}

// Returns the bottom of the column.
float ButtonsColumn(PageContext& context, ImVec2 origin, const Metrics& m) {
  SectionHeading(Tr(TextId::KeysButtonsHeading), origin);
  float y = origin.y + m.heading_height;
  for (std::size_t action = 0; action < kKeyButtonCount; ++action) {
    Row(context, action, KeyButtonName(action), ImVec2(origin.x, y), m);
    y += m.row_step;
  }
  return y;
}

float DirectionsColumn(PageContext& context, ImVec2 origin, float width, const Metrics& m) {
  constexpr struct {
    std::size_t first;
    TextId name;
  } kSections[] = {
      {kLeftStickFirst, TextId::LeftStickName},
      {kRightStickFirst, TextId::RightStickName},
      {kDpadFirst, TextId::DpadName},
  };
  float y = origin.y;
  for (const auto& section : kSections) {
    SectionHeading(Tr(section.name), ImVec2(origin.x, y));
    // The mouse moves the camera too.
    if (section.first == kRightStickFirst && context.config.mnk_mouse) {
      const char* tag = Tr(TextId::KeysPlusMouse);
      const float tag_width = ImGui::CalcTextSize(tag).x;
      ImGui::SetCursorScreenPos(ImVec2(origin.x + width - (m.slot_width + tag_width) / 2, y));
      ImGui::TextColored(kAccent, "%s", tag);
    }
    y += m.heading_height;
    for (std::size_t i = 0; i < 4; ++i) {
      Row(context, section.first + i, Tr(kDirections[i]), ImVec2(origin.x, y), m);
      y += m.row_step;
    }
    y += m.section_gap;
  }
  return y - m.section_gap;
}

}  // namespace

void RenderKeyMapping(PageContext& context, float height) {
  if (g_drawn_frame < ImGui::GetFrameCount() - 1) g_capture = {};
  g_drawn_frame = ImGui::GetFrameCount();
  UpdateCapture(context);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
  ImGui::BeginChild("KeyMapping", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  Heading(Tr(TextId::MapKeysTitle), 17);
  ImGui::PopStyleColor();
  ImGui::SameLine(0, 16);
  ImGui::TextDisabled("%s", Tr(TextId::MapKeysPath));
  ImGui::Spacing();

  ImGui::PushFont(nullptr, 15);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  constexpr float kColumnGap = 24;
  const float column = (width - kColumnGap) / 2;

  // The hint takes the bottom; the rows share what is left, up to 28 px each.
  const char* hint = Tr(TextId::KeysHint);
  ImGui::PushFont(nullptr, 13);
  const float hint_height = ImGui::CalcTextSize(hint, nullptr, false, width).y;
  ImGui::PopFont();
  Metrics m{};
  m.heading_height = ImGui::GetFontSize() + 6;
  m.section_gap = 8;
  const float rows_height = ImGui::GetContentRegionAvail().y - hint_height - 10;
  m.row_step = std::clamp((rows_height - 3 * m.heading_height - 2 * m.section_gap) / 12, 20.0f, 28.0f);
  m.row_height = m.row_step - 4;
  m.slot_gap = 6;
  float labels = 0;
  for (std::size_t action = 0; action < kKeyButtonCount; ++action) {
    labels = std::max(labels, ImGui::CalcTextSize(KeyButtonName(action)).x);
  }
  for (const TextId direction : kDirections) labels = std::max(labels, ImGui::CalcTextSize(Tr(direction)).x);
  m.label_width = labels + 14;
  m.slot_width = (column - m.label_width - m.slot_gap) / 2;

  const float left = ButtonsColumn(context, origin, m);
  const float right = DirectionsColumn(context, ImVec2(origin.x + column + kColumnGap, origin.y), column, m);
  ImGui::PopFont();

  ImGui::SetCursorScreenPos(ImVec2(origin.x, std::max(left, right) + 8));
  ImGui::PushFont(nullptr, 13);
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
  ImGui::TextDisabled("%s", hint);
  ImGui::PopTextWrapPos();
  ImGui::PopFont();
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

void RestoreDefaultKeys(PageContext& context) {
  g_capture = {};
  context.config.key_binds = DefaultKeyBinds();
  context.status_message = Tr(TextId::KeysRestoredStatus);
}

bool CaptureKeyEvent(const SDL_Event& event) {
  if (!g_capture.active) return false;
  switch (event.type) {
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      g_capture = {};
      return false;
    case SDL_EVENT_KEY_DOWN:
      // Keys the game cannot bind (the Windows keys) are passed over.
      if (!event.key.repeat && g_capture.key.empty()) {
        if (const char* name = KeyNameForScancode(event.key.scancode)) g_capture.key = name;
      }
      return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (g_capture.key.empty()) {
        if (const char* name = KeyNameForMouseButton(event.button.button)) g_capture.key = name;
      }
      return true;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_TEXT_INPUT:
      return true;
    default:
      return false;
  }
}

}  // namespace launcher_ui
