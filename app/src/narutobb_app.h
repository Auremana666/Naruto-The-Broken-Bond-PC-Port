// narutobb - Naruto: The Broken Bond, ReXGlue recompiled project.

#pragma once

#include <algorithm>
#include <filesystem>

#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>

#include <atomic>
#include <set>
#include <string>

#include <imgui.h>

#include <rex/cvar.h>

#include "fps_counter.h"
#include "options_menu.h"
#include "auto_jutsu.h"
#include "input_layout.h"
#include "pad_input.h"
#include "player_assignment.h"
#include "title_screen.h"
#include "ultrawide.h"

// Guest frame rate for the F3 overlay: the command processor times every guest swap and keeps the
// result in the perf counter registry, so this is the speed the game itself runs at.
inline rex::ui::FrameStats ReadGuestFrameStats() {
  rex::ui::FrameStats stats;
  const int64_t fps = rex::perf::GetSnapshotCounter(rex::perf::CounterId::kFps);
  if (fps > 0) {
    stats.fps = static_cast<double>(fps);
    stats.frame_time_ms =
        static_cast<double>(rex::perf::GetSnapshotCounter(rex::perf::CounterId::kFrameTimeUs)) /
        1000.0;
    stats.frame_count = 1;
  }
  return stats;
}

// Resource ids from assets/icon/naruto.rc.
constexpr int kMenuFontResource = 105;

class NarutobbApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<NarutobbApp>(new NarutobbApp(ctx, "naruto",
        PPCImageConfig));
  }

  // Finds the game when --game_data_root is not given, like the Black port: a disc image (.iso)
  // in assets/ (or next to the executable), else extracted files in game/ or game_root/.
  // Saves and other user data live in hdd/ next to the executable.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    paths.user_data_root = exe_dir / "hdd";
    std::error_code ec;
    std::filesystem::create_directories(paths.user_data_root, ec);

    if (!paths.game_data_root.empty() && std::filesystem::exists(paths.game_data_root)) {
      return;
    }

    for (const auto& folder : {exe_dir / "assets", exe_dir}) {
      for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        auto extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        if (entry.is_regular_file(ec) && extension == ".iso") {
          paths.game_data_root = entry.path();
          REXLOG_INFO("[narutobb] Disc image: {}", paths.game_data_root.string());
          return;
        }
      }
    }

    const std::filesystem::path candidates[] = {
        exe_dir / "game",
        exe_dir / "game_root",
        exe_dir / "../../../../game_root",
    };
    for (const auto& candidate : candidates) {
      std::error_code ec;
      if (std::filesystem::is_regular_file(candidate / "default.xex", ec)) {
        paths.game_data_root = std::filesystem::canonical(candidate, ec);
        REXLOG_INFO("[narutobb] Game files: {}", paths.game_data_root.string());
        return;
      }
    }
  }

  // The window is titled with the game's name from the start (before the game is found).
  bool SetupPresentation() override {
    if (!rex::ReXApp::SetupPresentation()) return false;
    if (window()) window()->SetTitle("Naruto The Broken Bond PC");
    return true;
  }

  // No disc image or game files found: say where to put them.
  bool ConstructRuntime(const rex::PathConfig& paths) override {
    if (paths.game_data_root.empty()) {
      REXLOG_ERROR("[narutobb] No game files found");
      ShowGameError(
          "The game files were not found.\n\n"
          "Put the game's ISO file (the Xbox 360 disc image) in the assets folder, or in the same "
          "folder as Naruto The Broken Bond PC.exe, then start the game again.");
      return false;
    }
    return rex::ReXApp::ConstructRuntime(paths);
  }

  // The game-style menu font (Titan One), built into the executable.
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    const void* data = nullptr;
    size_t size = 0;
    if (!ReadResource(kMenuFontResource, &data, &size)) {
      REXLOG_WARN("[narutobb] Menu font missing from the executable");
      return;
    }
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;  // the data stays in the executable's resources
    config.OversampleH = 2;
    config.OversampleV = 2;
    // Baked large and drawn smaller, so the menu stays sharp up to 4K.
    g_menu_font = atlas->AddFontFromMemoryTTF(const_cast<void*>(data), static_cast<int>(size),
                                              96.0f, &config, atlas->GetGlyphRangesDefault());
  }

  void OnPostSetup() override {
    if (window()) {
      window()->SetTitle("Naruto The Broken Bond PC");
    }
    SetGuestFrameStats(&ReadGuestFrameStats);
    if (imgui_drawer()) {
      // Also checks the controller shortcut for the options menu every frame.
      fps_counter_ = new FpsCounter(imgui_drawer(), &ReadGuestFrameStats,
                                    [this]() { PollControllerShortcut(); });
      // PC title screen (PC Options > Game): the game boots underneath and gets no input.
      if (rex::cvar::GetFlagByName("show_title_screen") == "true") {
        title_open_.store(true);
        title_ = new TitleScreen(imgui_drawer(), rex::filesystem::GetExecutableFolder() / "title",
                                 [this]() {
                                   title_ = nullptr;
                                   title_open_.store(false);
                                   hold_input_.store(true);
                                 });
      }
    }

    rex::ui::RegisterBind("bind_exit_game", "Alt+F4", "Exit Game", [this]() {
      if (window()) {
        window()->RequestClose();
      }
    });

    // F1: PC options menu. The game stops reading input while it is open, and after it closes
    // until the controller buttons are released, so the press that closed it doesn't reach the game.
    rex::ui::RegisterBind("bind_options", "F1", "Options", [this]() { ToggleOptions(); });
    if (auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system())) {
      // Keyboard as player 1 or 2 (PC Options > Controls). Installed before the game polls input.
      input->SetDeviceAssignment(std::make_unique<PlayerAssignment>());
      // Controller remapping and toggle / turbo presses (PC Options > Controls).
      InstallInputLayout(input);
      // Auto Jutsu keys (PC Options > Cheats).
      InstallAutoJutsu(window(), runtime()->virtual_membase());
      input->SetActiveCallback([this]() {
        if (options_open_.load() || title_open_.load()) return false;
        if (hold_input_.load()) {
          if (ReadPads() != 0) return false;
          hold_input_.store(false);
        }
        return true;
      });
    }

    // The SDK's detailed overlay (F3) has no key unless the player picks one in Shortcut Keys.
    // The SDK's own settings overlay (F4) and console (backtick) are replaced by PC Options.
    for (const char* bind : {"bind_debug_overlay", "bind_settings", "bind_console"}) {
      if (rex::cvar::GetFlagSource(bind) == rex::cvar::Source::kDefault) {
        rex::cvar::SetFlagByName(bind, "");
      }
    }

    // Shortcut keys that open the menu on a given page (PC Options > Shortcut Keys).
    for (const auto& sc : ShortcutList()) {
      if (sc.page < 0) continue;
      const int page = sc.page;
      rex::ui::RegisterBind(sc.cvar, sc.default_key, sc.label, [this, page]() { OpenOptionsAt(page); });
    }

    // Ctrl + F12: log files in logs/ on or off (off by default), saved to naruto.toml. Binds take
    // a single key, so the key is F12 and Ctrl is checked here.
    rex::ui::RegisterBind("bind_toggle_logs", "F12", "Toggle log files", [this]() {
      if (options_ && options_->IsCapturing()) return;
      if (!CtrlHeld()) return;
      const bool on = !rex::IsFileLogging();
      rex::SetFileLogging(on);
      rex::cvar::SetFlagByName("log_to_file", on ? "true" : "false");
      SaveSettings(ConfigPath(), {"log_to_file"});
      ShowToast(on ? "LOG FILES: ON" : "LOG FILES: OFF");
    });

    // F2 (rebindable in PC Options > Shortcut Keys): FPS counter on/off, saved to naruto.toml.
    rex::ui::RegisterBind("bind_fps_counter", "F2", "Toggle FPS counter", [this]() {
      if (options_ && options_->IsCapturing()) return;
      const bool on = rex::cvar::GetFlagByName("show_fps") == "true";
      rex::cvar::SetFlagByName("show_fps", on ? "false" : "true");
      SaveSettings(ConfigPath(), {"show_fps"});
    });
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_exit_game");
    rex::ui::UnregisterBind("bind_options");
    rex::ui::UnregisterBind("bind_fps_counter");
    rex::ui::UnregisterBind("bind_toggle_logs");
    for (const auto& sc : ShortcutList()) {
      if (sc.page >= 0) rex::ui::UnregisterBind(sc.cvar);
    }
    if (options_) {
      options_->Save();
    }
  }

 private:
  static std::filesystem::path ConfigPath() {
    return rex::filesystem::GetExecutableFolder() / "naruto.toml";
  }

  // Back + Start on a controller opens or closes the PC options menu.
  void PollControllerShortcut() {
    // Also the per-frame place for the aspect ratio setting (PC Options > Display).
    UpdateUltrawide(runtime() ? runtime()->virtual_membase() : nullptr);
    const uint32_t pads = ReadPads();
    const bool held = (pads & kPadBack) && (pads & kPadStart);
    if (held && !shortcut_held_) {
      ToggleOptions();
    }
    shortcut_held_ = held;
  }

  void OpenOptionsAt(int page) {
    if (options_ && options_->IsCapturing()) return;
    if (!options_) ToggleOptions();
    if (options_) options_->JumpTo(page);
  }

  void ToggleOptions() {
    if (options_ && options_->IsCapturing()) return;  // the key is being bound
    if (options_) {
      options_->RequestClose();
      return;
    }
    if (!imgui_drawer()) {
      return;
    }
    options_ = new OptionsMenu(
        imgui_drawer(), ConfigPath(),
        [this]() {
          options_ = nullptr;
          options_open_.store(false);
          hold_input_.store(true);
        },
        [this]() {
          if (window()) window()->RequestClose();
        });
    options_open_.store(true);
  }

  OptionsMenu* options_ = nullptr;  // owned by the ImGui drawer; deletes itself on close
  FpsCounter* fps_counter_ = nullptr;
  TitleScreen* title_ = nullptr;  // owned by the ImGui drawer; deletes itself on close
  std::atomic<bool> title_open_ = false;  // read from the game's input thread
  bool shortcut_held_ = false;
  std::atomic<bool> options_open_ = false;  // read from the game's input thread
  std::atomic<bool> hold_input_ = false;
};
