#include "fps_counter.h"

#include <chrono>
#include <cstdio>
#include <mutex>

#include <imgui.h>

#include <rex/cvar.h>

#include "options_menu.h"

REXCVAR_DEFINE_BOOL(show_fps, false, "Display", "Show the frame rate counter in the top right corner");

namespace {
std::mutex g_toast_mutex;
std::string g_toast;
std::chrono::steady_clock::time_point g_toast_time;

// The toast, centered at the top, fading out after two seconds.
void DrawToast(ImGuiIO& io) {
  std::string text;
  float age;
  {
    std::lock_guard lock(g_toast_mutex);
    if (g_toast.empty()) return;
    age = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_toast_time).count();
    if (age > 2.5f) {
      g_toast.clear();
      return;
    }
    text = g_toast;
  }
  const float alpha = age < 2.0f ? 1.0f : 1.0f - (age - 2.0f) / 0.5f;
  ImFont* font = g_menu_font ? g_menu_font : ImGui::GetFont();
  const float s = io.DisplaySize.y / 1080.0f;
  const float size = 38.0f * s;
  const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
  const ImVec2 pos((io.DisplaySize.x - extent.x) * 0.5f, 40.0f * s);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const int a = static_cast<int>(255 * alpha);
  dl->AddRectFilled(ImVec2(pos.x - 30 * s, pos.y - 14 * s),
                    ImVec2(pos.x + extent.x + 30 * s, pos.y + extent.y + 14 * s),
                    IM_COL32(6, 12, 38, a * 200 / 255), 12 * s);
  dl->AddText(font, size, pos, IM_COL32(255, 140, 24, a), text.c_str());
}
}  // namespace

void ShowToast(std::string text) {
  std::lock_guard lock(g_toast_mutex);
  g_toast = std::move(text);
  g_toast_time = std::chrono::steady_clock::now();
}

void FpsCounter::OnDraw(ImGuiIO& io) {
  if (tick_) {
    tick_();
  }
  DrawToast(io);
  if (!REXCVAR_GET(show_fps) || !stats_) {
    return;
  }
  const auto stats = stats_();
  if (stats.frame_count == 0) {
    return;
  }
  char text[32];
  std::snprintf(text, sizeof(text), "%.0f FPS", stats.fps);

  ImFont* font = g_menu_font ? g_menu_font : ImGui::GetFont();
  const float s = io.DisplaySize.y / 1080.0f;
  const float size = 34.0f * s;
  const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
  const ImVec2 pos(io.DisplaySize.x - extent.x - 40.0f * s, 30.0f * s);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float o = 2.5f * s;
  for (float dx = -1; dx <= 1; ++dx) {
    for (float dy = -1; dy <= 1; ++dy) {
      if (dx != 0 || dy != 0) {
        dl->AddText(font, size, ImVec2(pos.x + dx * o, pos.y + dy * o), IM_COL32(6, 12, 38, 255),
                    text);
      }
    }
  }
  // Orange like the game's highlights, white at the target 30 FPS.
  const ImU32 color = stats.fps >= 29.0 ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 140, 24, 255);
  dl->AddText(font, size, pos, color, text);
}
