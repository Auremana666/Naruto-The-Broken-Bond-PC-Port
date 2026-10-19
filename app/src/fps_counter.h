// Frame rate counter in the top right corner, drawn in the menu font (PC Options > Display).

#pragma once

#include <functional>
#include <string>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/overlay/debug_overlay.h>

// A short message shown at the top of the screen for a moment (for example "LOG FILES: ON").
void ShowToast(std::string text);

class FpsCounter : public rex::ui::ImGuiDialog {
 public:
  // tick runs every frame before drawing (the app uses it for its controller shortcut).
  FpsCounter(rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::FrameStats()> stats,
             std::function<void()> tick)
      : ImGuiDialog(drawer), stats_(std::move(stats)), tick_(std::move(tick)) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  std::function<rex::ui::FrameStats()> stats_;
  std::function<void()> tick_;
};
