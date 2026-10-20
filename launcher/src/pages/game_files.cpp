#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <mutex>
#include <string>

#include "folders.h"
#include "game_paths.h"
#include "localization.h"
#include "pages/pages.h"
#include "widgets.h"

namespace launcher_ui {
namespace {

// The folder dialog answers on another thread.
std::mutex g_dialog_mutex;
std::string g_selected_folder;
bool g_dialog_pending = false;
bool g_dialog_updated = false;

void SDLCALL FolderDialogCallback(void*, const char* const* files, int) {
  std::lock_guard lock(g_dialog_mutex);
  if (files && files[0]) {
    g_selected_folder = files[0];
    g_dialog_updated = true;
  }
  g_dialog_pending = false;
}

bool DialogPending() {
  std::lock_guard lock(g_dialog_mutex);
  return g_dialog_pending;
}

}  // namespace

void ApplyGameFolderSelection(PageContext& context) {
  std::lock_guard lock(g_dialog_mutex);
  if (!g_dialog_updated) return;
  g_dialog_updated = false;
  std::error_code error;
  if (std::filesystem::is_regular_file(game_paths::FromUtf8(g_selected_folder) / "default.xex", error)) {
    context.config.custom_game_root = g_selected_folder;
    context.status_message = Tr(TextId::PathUpdatedMsg) + g_selected_folder;
  } else {
    context.status_message = Tr(TextId::FolderHint);
  }
}

void RenderGameFilesPage(PageContext& context) {
  const GameStatus& game = context.game;
  Heading(Tr(TextId::TabGameFiles));
  Muted(Tr(TextId::FilesIntro));
  ImGui::Dummy(ImVec2(0, 16));

  BeginCard("Folder", 0);
  Heading(Tr(TextId::FolderHeading), 21);
  ImGui::TextColored(game.FilesFound() ? kGreen : kWarning, "%s",
                     Tr(game.FilesFound() ? TextId::FilesDetected : TextId::StatusNotFound));
  ImGui::Spacing();
  // Scrollable path: long locations cannot overlap the actions.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
  ImGui::BeginChild("FolderPath", ImVec2(0, 48), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  const std::string path = game.FilesFound() ? PathText(game.game_root)
                           : context.config.custom_game_root.empty() ? Tr(TextId::NoneDetected)
                                                                     : context.config.custom_game_root;
  ImGui::TextUnformatted(path.c_str());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
  ImGui::EndChild();
  ImGui::PopStyleVar();
  Muted(Tr(TextId::FolderHint));
  ImGui::Spacing();
  ImGui::BeginDisabled(DialogPending());
  if (PrimaryButton(Tr(TextId::BtnSelectFolder), ImVec2(-1, 38))) {
    { std::lock_guard lock(g_dialog_mutex); g_dialog_pending = true; }
    SDL_ShowOpenFolderDialog(FolderDialogCallback, nullptr, context.window, nullptr, false);
  }
  ImGui::EndDisabled();
  EndCard();

  const float half_width = (ImGui::GetContentRegionAvail().x - 10) / 2;
  if (ImGui::Button(Tr(TextId::BtnResetDefault), ImVec2(half_width, 36))) {
    context.config.custom_game_root.clear();
    context.status_message = Tr(TextId::PathResetMsg);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!game.FilesFound());
  if (ImGui::Button(Tr(TextId::BtnOpenFolder), ImVec2(half_width, 36))) OpenFolder(game.game_root);
  ImGui::EndDisabled();
  ImGui::Dummy(ImVec2(0, 14));

  BeginCard("SetupHelp", 0);
  ImGui::TextColored(kAccent, "%s", Tr(TextId::SetupHeading));
  ImGui::Spacing();
  Muted(Tr(TextId::SetupHint));
  EndCard();
}

}  // namespace launcher_ui
