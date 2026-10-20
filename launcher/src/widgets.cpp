#include "widgets.h"

namespace launcher_ui {

void PushBoldFont(float size) {
  auto& fonts = ImGui::GetIO().Fonts->Fonts;
  ImGui::PushFont(fonts.Size > 1 ? fonts[1] : ImGui::GetIO().FontDefault, size);
}

void Heading(const char* text, float size) {
  PushBoldFont(size);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

void Muted(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

bool PrimaryButton(const char* label, ImVec2 size) {
  ImGui::PushStyleColor(ImGuiCol_Button, kPrimary);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kPrimaryHovered);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kPrimaryActive);
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor(3);
  return clicked;
}

void BeginCard(const char* id, float height) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
  ImGuiChildFlags flags = ImGuiChildFlags_Borders;
  if (height == 0) flags |= ImGuiChildFlags_AutoResizeY;
  ImGui::BeginChild(id, ImVec2(0, height), flags,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

}  // namespace launcher_ui
