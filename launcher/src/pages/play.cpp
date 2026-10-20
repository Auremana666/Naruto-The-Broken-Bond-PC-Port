#include <algorithm>

#include "localization.h"
#include "pages/pages.h"
#include "widgets.h"

namespace launcher_ui {

void RenderPlayPage(PageContext& context) {
  const GameStatus& game = context.game;
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = ImGui::GetContentRegionAvail().y;

  ImGui::Spacing();
  Heading(Tr(TextId::PlayHeading));
  Muted(Tr(TextId::PlayIntro));
  ImGui::Dummy(ImVec2(0, 24));

  BeginCard("ReadyCard", 0);
  const ImVec4 state_color = game.Ready() ? kGreen : kWarning;
  const ImVec2 dot = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(dot.x + 6, dot.y + 13), 5,
                                              ImGui::ColorConvertFloat4ToU32(state_color));
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22);
  ImGui::PushStyleColor(ImGuiCol_Text, state_color);
  Heading(Tr(!game.FilesFound() ? TextId::StatusNotFound :
             !game.executable_found ? TextId::ExeMissingStatus : TextId::StatusReady), 21);
  ImGui::PopStyleColor();
  ImGui::Spacing();
  Muted(Tr(game.Ready() ? TextId::ReadyDetail :
           !game.FilesFound() ? TextId::FilesMissingDesc : TextId::ExeMissing));
  EndCard();

  // One obvious main action, centered, with the way to fix missing files below it.
  ImGui::Dummy(ImVec2(0, std::max(20.0f, height * 0.10f)));
  const float button_width = std::min(300.0f, width - 40);
  const float button_x = (width - button_width) / 2;
  ImGui::SetCursorPosX(button_x);
  ImGui::BeginDisabled(!game.Ready());
  PushBoldFont(27);
  if (PrimaryButton(Tr(TextId::BtnPlay), ImVec2(button_width, 74))) context.request_launch = true;
  ImGui::PopFont();
  ImGui::EndDisabled();
  ImGui::Spacing();
  ImGui::SetCursorPosX(button_x);
  if (ImGui::Button(Tr(TextId::ManageFiles), ImVec2(button_width, 36))) context.page = Page::kGameFiles;
}

}  // namespace launcher_ui
