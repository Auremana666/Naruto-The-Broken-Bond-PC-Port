#pragma once

// Frame display reused from the Rise of a Ninja launcher integration.
#include <array>
#include <functional>
#include <utility>
#include <rex/perf/counter.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/overlay/debug_overlay.h>

// Guest frame stats source for the F1/F3 overlays. The command processor
// measures the frame time on every guest swap (XE_SWAP) and snapshots it in
// the perf counter registry; game speed follows this rate, not the host
// window present rate. Returns frame_count == 0 when the SDK was built
// without REXGLUE_ENABLE_PERF_COUNTERS (guest rate unavailable).
inline rex::ui::FrameStats ReadGuestFrameStats() {
  const int64_t fps = rex::perf::GetSnapshotCounter(rex::perf::CounterId::kFps);
  const int64_t ft_us =
      rex::perf::GetSnapshotCounter(rex::perf::CounterId::kFrameTimeUs);
  rex::ui::FrameStats stats;
  if (fps > 0) {
    stats.fps = static_cast<double>(fps);
    stats.frame_time_ms = static_cast<double>(ft_us) / 1000.0;
    stats.frame_count = 1;  // valid data this frame
  }
  return stats;
}

class FpsOverlayDialog : public rex::ui::ImGuiDialog {
 public:
  using GuestStatsProvider = std::function<rex::ui::FrameStats()>;

  explicit FpsOverlayDialog(rex::ui::ImGuiDrawer* drawer,
                            GuestStatsProvider guest_stats = {})
      : rex::ui::ImGuiDialog(drawer), guest_stats_(std::move(guest_stats)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    // The host rate counts window presents (io.Framerate), which can run far
    // above the game's own frame rate when presentation is independent
    // (high-refresh display, tearing allowed). Game speed follows the guest
    // rate, so the overlay leads with it and keeps the host rate as context.
    rex::ui::FrameStats guest{};
    if (guest_stats_) {
      guest = guest_stats_();
    }
    const bool have_guest = guest.frame_count > 0;

    const double host_fps = io.Framerate;
    const double fps = have_guest ? guest.fps : host_fps;
    const double ft_ms =
        have_guest ? guest.frame_time_ms : io.DeltaTime * 1000.0;

    if (smoothed_fps_ == 0.0) {
      smoothed_fps_ = fps;
      smoothed_ft_ = ft_ms;
      smoothed_host_fps_ = host_fps;
    } else {
      smoothed_fps_ = smoothed_fps_ * 0.85 + fps * 0.15;
      smoothed_ft_ = smoothed_ft_ * 0.85 + ft_ms * 0.15;
      smoothed_host_fps_ = smoothed_host_fps_ * 0.85 + host_fps * 0.15;
    }

    frame_history_[history_idx_] = static_cast<float>(smoothed_ft_);
    history_idx_ = (history_idx_ + 1) % kHistorySize;

    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

    bool visible = true;
    if (ImGui::Begin("##fps_overlay", &visible,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing)) {
      ImGui::SetWindowFontScale(2.0f);
      ImU32 fps_color = smoothed_fps_ >= 55.0f ? IM_COL32(80, 255, 80, 255) :
                       smoothed_fps_ >= 30.0f ? IM_COL32(255, 220, 60, 255) :
                                                IM_COL32(255, 80, 80, 255);
      ImGui::PushStyleColor(ImGuiCol_Text, fps_color);
      ImGui::Text("%s: %.0f FPS", have_guest ? "Guest" : "Host", smoothed_fps_);
      ImGui::PopStyleColor();
      ImGui::SetWindowFontScale(1.0f);

      ImGui::SetWindowFontScale(1.3f);
      ImGui::Text("%.1f ms", smoothed_ft_);
      ImGui::SetWindowFontScale(1.0f);

      if (have_guest) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, 255));
        ImGui::Text("Host: %.0f FPS", smoothed_host_fps_);
        ImGui::PopStyleColor();
      }

      ImGui::Spacing();
      ImGui::PlotLines("##frametime", frame_history_.data(),
                       static_cast<int>(kHistorySize),
                       static_cast<int>(history_idx_), nullptr,
                       0.0f, 50.0f, ImVec2(220, 50));
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }

 private:
  static constexpr size_t kHistorySize = 120;
  std::array<float, kHistorySize> frame_history_{};
  size_t history_idx_ = 0;
  double smoothed_fps_ = 0.0;
  double smoothed_ft_ = 0.0;
  double smoothed_host_fps_ = 0.0;
  GuestStatsProvider guest_stats_;
};
