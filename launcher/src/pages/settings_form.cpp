#include "pages/settings_form.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include "config_manager.h"
#include "widgets.h"

namespace launcher_ui {
namespace {

// The option the user pointed at last, kept across frames and groups.
const Option* g_remembered = nullptr;
// The label column, as wide as the widest label of the last group drawn.
float g_label_width = 160;

constexpr float kRowSpacing = 4;
constexpr float kHighlightMargin = 8;
constexpr ImU32 kHighlightFill = IM_COL32(42, 33, 25, 255);
constexpr ImU32 kHighlightBorder = IM_COL32(122, 64, 31, 255);

bool Shown(const Option& option) {
  return option.renderer != Renderer::kNative || kNativeRendererBuilt;
}

bool UsedBy(const Option& option, bool native) {
  return option.available &&
      (option.renderer == Renderer::kBoth || (option.renderer == Renderer::kNative) == native);
}

// The "only in one renderer" tag, in a smaller size of the current font.
const char* TagText(const Option& option) {
  if (!option.available) return Tr(TextId::UnavailableTag);
  return Tr(option.renderer == Renderer::kXenos ? TextId::XenosOnlyTag : TextId::NativeOnlyTag);
}

float TagFontSize() { return ImGui::GetFontSize() * 0.8f; }

ImVec2 TagSize(const char* tag) { return ImGui::GetFont()->CalcTextSizeA(TagFontSize(), FLT_MAX, 0, tag); }

}  // namespace

OptionList::OptionList(bool native)
    : native_(native), row_width_(ImGui::GetContentRegionAvail().x), label_width_(g_label_width) {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, kRowSpacing));
}

OptionList::~OptionList() {
  ImGui::PopStyleVar();
  if (pointed_) g_remembered = pointed_;
  // The next frame's column fits this frame's labels, up to half of the row.
  if (widest_label_ > 0) g_label_width = std::min(widest_label_ + 24, row_width_ / 2);
}

bool OptionList::Combo(const Option& option, int& index, std::span<const char* const> items) {
  if (!BeginRow(option)) return false;
  LabelColumn(option);
  const bool changed = ImGui::Combo("##value", &index, items.data(), static_cast<int>(items.size()));
  DrawTag(option, ImGui::GetItemRectMax().x - ImGui::GetFrameHeight() - 6, true);
  EndRow(option);
  return changed;
}

bool OptionList::Checkbox(const Option& option, bool& value) {
  if (!BeginRow(option)) return false;
  const bool changed = ImGui::Checkbox(Tr(option.label), &value);
  DrawTag(option, ImGui::GetItemRectMax().x + 12, false);
  EndRow(option);
  return changed;
}

bool OptionList::Slider(const Option& option, int& value, int min, int max, const char* format) {
  return Track(option, value, min, max, {}, format);
}

bool OptionList::Slider(const Option& option, int& value, std::span<const int> stops) {
  return !stops.empty() && Track(option, value, 0, 0, stops, "%d");
}

bool OptionList::Track(const Option& option, int& value, int min, int max, std::span<const int> stops,
                       const char* format) {
  if (!BeginRow(option)) return false;
  LabelColumn(option);
  // The value (or the tag, when the renderer does not use the option) takes a
  // column on the right, as wide as the widest value; the track takes the rest.
  const bool used = UsedBy(option, native_);
  char widest[16];
  std::snprintf(widest, sizeof(widest), format, stops.empty() ? max : stops.back());
  const float value_width = std::max(ImGui::CalcTextSize("000").x, ImGui::CalcTextSize(widest).x);
  const float side = (used ? value_width : TagSize(TagText(option)).x) + 12;
  ImGui::SetNextItemWidth(row_start_.x + row_width_ - ImGui::GetCursorScreenPos().x - side);

  // With stops, ImGui's slider runs over a fine range, so its grab is narrow and
  // the handle reaches both ends; the value snaps to the nearest stop.
  constexpr int kFine = 1000;
  const int last = static_cast<int>(stops.size()) - 1;
  int index = 0;
  for (int i = 1; i <= last; ++i) {
    if (std::abs(static_cast<int64_t>(stops[i]) - value) <
        std::abs(static_cast<int64_t>(stops[index]) - value)) index = i;
  }
  const int low = stops.empty() ? min : 0;
  const int high = stops.empty() ? max : kFine;
  int position = stops.empty() ? value : last > 0 ? index * kFine / last : 0;

  // ImGui's slider keeps the mouse and keyboard handling, drawn invisible.
  constexpr ImGuiCol kHidden[] = {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
                                  ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive};
  for (const ImGuiCol color : kHidden) ImGui::PushStyleColor(color, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  bool changed = ImGui::SliderInt("##value", &position, low, high, "");
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(static_cast<int>(std::size(kHidden)));
  if (stops.empty()) {
    value = position;
  } else {
    int snapped = last > 0 ? (position * last + kFine / 2) / kFine : 0;
    // The arrows move from one stop to the next (the fine range would take a
    // step of 1 out of kFine).
    if (ImGui::IsItemFocused()) {
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) snapped = index - 1;
      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) snapped = index + 1;
    }
    snapped = std::clamp(snapped, 0, last);
    changed = snapped != index;
    index = snapped;
    if (changed) value = stops[index];
  }

  // The handle sits where ImGui's grab is: within a 2-pixel padding, over the
  // width left by the grab (SliderBehaviorT).
  const ImVec2 item_min = ImGui::GetItemRectMin();
  const ImVec2 item_max = ImGui::GetItemRectMax();
  constexpr float kGrabPadding = 2;
  const float slider_width = item_max.x - item_min.x - 2 * kGrabPadding;
  const float grab = std::min(slider_width, std::max(slider_width / (high - low + 1), ImGui::GetStyle().GrabMinSize));
  const float fraction = stops.empty() ? (max > min ? static_cast<float>(value - min) / (max - min) : 0.0f)
                                       : (last > 0 ? static_cast<float>(index) / last : 0.0f);
  const float x = item_min.x + kGrabPadding + grab / 2 + fraction * (slider_width - grab);
  const int shown = value;  // The configured value, also when it lies between the stops.
  const float y = (item_min.y + item_max.y) / 2;
  auto* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(ImVec2(item_min.x, y - 3), ImVec2(item_max.x, y + 3), ImGui::GetColorU32(ImGuiCol_FrameBg), 3);
  draw->AddRectFilled(ImVec2(item_min.x, y - 3), ImVec2(x, y + 3), ImGui::GetColorU32(kPrimary), 3);
  draw->AddRectFilled(ImVec2(x - 6, y - 10), ImVec2(x + 6, y + 10), ImGui::GetColorU32(ImGuiCol_Text), 4);
  if (used) {
    char text[16];
    std::snprintf(text, sizeof(text), format, shown);
    draw->AddText(ImVec2(item_max.x + 12, y - ImGui::GetFontSize() / 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
  } else {
    DrawTag(option, item_max.x + 12, false);
  }
  EndRow(option);
  return changed;
}

void OptionList::Text(const Option& option, const char* text) {
  if (!BeginRow(option)) return;
  LabelColumn(option);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", text);
  EndRow(option);
}

bool OptionList::Button(const Option& option, const char* text) {
  if (!BeginRow(option)) return false;
  LabelColumn(option);
  const bool clicked = ImGui::Button(text, ImVec2(row_width_ - label_width_, 0));
  EndRow(option);
  return clicked;
}

const Option* OptionList::Described() const {
  if (pointed_) return pointed_;
  return remembered_drawn_ ? g_remembered : first_;
}

bool OptionList::BeginRow(const Option& option) {
  if (!Shown(option)) return false;
  if (!first_) first_ = &option;
  row_start_ = ImGui::GetCursorScreenPos();
  if (&option == g_remembered) {
    remembered_drawn_ = true;
    const ImVec2 min(row_start_.x - kHighlightMargin, row_start_.y - kRowSpacing / 2);
    const ImVec2 max(row_start_.x + row_width_ + kHighlightMargin,
                     row_start_.y + ImGui::GetFrameHeight() + kRowSpacing / 2);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, kHighlightFill, 3);
    draw->AddRect(min, max, kHighlightBorder, 3);
  }
  ImGui::PushID(&option);
  ImGui::BeginDisabled(!UsedBy(option, native_));
  return true;
}

void OptionList::EndRow(const Option& option) {
  ImGui::EndDisabled();
  ImGui::PopID();
  // The mouse counts over the whole row, label included, unless a popup
  // covers it. A clicked control keeps the keyboard focus, so the focus only
  // counts while the keyboard navigates, and the mouse wins over it.
  const ImVec2 min(row_start_.x - kHighlightMargin, row_start_.y - kRowSpacing / 2);
  const ImVec2 max(row_start_.x + row_width_ + kHighlightMargin,
                   row_start_.y + ImGui::GetFrameHeight() + kRowSpacing / 2);
  if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max)) {
    pointed_ = &option;
  } else if (!pointed_ && ImGui::GetIO().NavVisible && ImGui::IsItemFocused()) {
    pointed_ = &option;
  }
}

void OptionList::LabelColumn(const Option& option) {
  const char* label = Tr(option.label);
  widest_label_ = std::max(widest_label_, ImGui::CalcTextSize(label).x);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  ImGui::SetCursorScreenPos(ImVec2(row_start_.x + label_width_, row_start_.y));
  ImGui::SetNextItemWidth(row_width_ - label_width_);
}

void OptionList::DrawTag(const Option& option, float x, bool right_aligned) const {
  if (UsedBy(option, native_)) return;
  // Drawn directly, so it keeps full opacity beside the greyed-out control.
  const char* tag = TagText(option);
  const ImVec2 text = TagSize(tag);
  const float top = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y - text.y) / 2;
  ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), TagFontSize(), ImVec2(right_aligned ? x - text.x : x, top),
                                      ImGui::ColorConvertFloat4ToU32(kWarning), tag);
}

void RenderDescription(const Option& option, bool native) {
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  Heading(Tr(option.label), 19);
  ImGui::PopStyleColor();
  const bool native_text = native && option.native_description != TextId::Count;
  ImGui::TextWrapped("%s", Tr(native_text ? option.native_description : option.description));
  if (!UsedBy(option, native)) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextWrapped("%s", Tr(!option.available ? TextId::AspectUnavailableReason :
                               option.renderer == Renderer::kXenos ? TextId::XenosOnlyReason
                                                                   : TextId::NativeOnlyReason));
    ImGui::PopStyleColor();
  }
}

}  // namespace launcher_ui
