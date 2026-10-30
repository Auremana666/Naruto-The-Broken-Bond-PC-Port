// broken_bond - ReXGlue Recompiled Project
//
// The host for the recompiled game: the native renderer, and the PC features
// of Naruto The Broken Bond PC (options menu, title screen, controller layouts,
// Auto Jutsu, ultrawide) drawn through the native renderer's overlay.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>

#include <fmt/chrono.h>
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>

#include "graphics_device.h"
#include "input_replay.h"
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
#include "gpu/host/app.h"
#include "gpu/overlay.h"

namespace gpu {
void StartIdlePresenter();  // gpu/d3d/present.cpp
void StopIdlePresenter();
}  // namespace gpu
#endif
#include "pc/auto_jutsu.h"
#include "pc/fps_counter.h"
#include "pc/free_camera.h"
#include "pc/game_location.h"
#include "pc/game_speed.h"
#include "pc/input_layout.h"
#include "pc/mouse_camera.h"
#include "pc/options_menu.h"
#include "pc/overlay_drawer.h"
#include "pc/pad_input.h"
#include "pc/player_assignment.h"
#include "pc/title_screen.h"
#include "pc/ultrawide.h"

// The frame rate shown by the FPS counter: the frames the native renderer
// presents (with interpolation above 30, more than the game's logic steps).
inline rex::ui::FrameStats ReadGuestFrameStats() {
  rex::ui::FrameStats stats;
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  const double fps = gpu::overlay::PresentRate();
  if (fps > 0.0) {
    stats.fps = fps;
    stats.frame_time_ms = 1000.0 / fps;
    stats.frame_count = 1;
  }
#endif
  return stats;
}

// Resource ids from assets/icon/naruto.rc.
constexpr int kMenuFontResource = 105;

class BrokenBondApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    // "naruto": the settings file is naruto.toml next to the executable.
    return std::unique_ptr<BrokenBondApp>(new BrokenBondApp(ctx, "naruto",
        PPCImageConfig));
  }

  // Finds the game when game_data_root is not set: a disc image (.iso) in
  // assets/ (or next to the executable), else extracted files in game/ or
  // game_root/, else asks where it is (PickGameLocation) and saves the answer as
  // game_data_root. Saves and other user data live in hdd/ next to the executable.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    if (rex::cvar::GetFlagByName("user_data_root").empty()) {
      paths.user_data_root = exe_dir / "hdd";
    }
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

    for (const auto& candidate : {exe_dir / "game", exe_dir / "game_root"}) {
      if (std::filesystem::is_regular_file(candidate / "default.xex", ec)) {
        paths.game_data_root = std::filesystem::canonical(candidate, ec);
        REXLOG_INFO("[narutobb] Game files: {}", paths.game_data_root.string());
        return;
      }
    }

    paths.game_data_root = PickGameLocation();
    if (!paths.game_data_root.empty()) {
      REXLOG_INFO("[narutobb] Game picked: {}", paths.game_data_root.string());
      rex::cvar::SetFlagByName("game_data_root", paths.game_data_root.string());
      SaveSettings(exe_dir / "naruto.toml", {"game_data_root"});
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
          "Start the game again and pick the game's ISO file (the Xbox 360 disc image), its "
          "default.xex, or an archive (ZIP, RAR, 7Z, ZAR) with one of them. You can also put the "
          "ISO file in the assets folder.");
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

  void OnPreSetup(rex::RuntimeConfig& config) override {
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    // record_pipelines: the graphics pipelines this session creates are saved
    // at exit to pipeline-runs/<time>/pipelines.json next to the executable,
    // for scripts/update-pipeline-cache.ps1 (no first-time stutter in the next build).
    if (rex::cvar::GetFlagByName("record_pipelines") == "true" && !std::getenv("REX_NATIVE_FRAME_DIR")) {
      const auto folder = rex::filesystem::GetExecutableFolder() / "pipeline-runs" /
                          fmt::format("{:%Y%m%d-%H%M%S}", std::chrono::floor<std::chrono::seconds>(
                                                               std::chrono::system_clock::now()));
      std::error_code ec;
      std::filesystem::create_directories(folder, ec);
      _wputenv_s(L"REX_NATIVE_FRAME_DIR", folder.wstring().c_str());
      _wputenv_s(L"REX_NATIVE_PERF_LOG", L"1");  // performance.csv there too (gpu/diagnostics/perf_log.h).
    }
#endif
    // Input recording and replay for test boots (input_replay.cpp); the SDK's
    // default input system when neither is asked for.
    config.input_factory = CreateInputSystem;
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    // The native renderer replaces the Xenos plugin when renderer = "native".
    gpu::host::Configure(config);
#endif
    if (!config.graphics) SelectXenosAdapter();
  }

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  // The SDK's overlay runs detached: recorded on the UI thread, drawn by the
  // native renderer's present, which asks for the next frame at every present.
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
    auto drawer = std::make_unique<OverlayDrawer>();
    overlay_drawer_ = drawer.get();
    gpu::overlay::SetFrameRequest([this] {
      app_context().CallInUIThread([this] { DrawOverlayFrame(); });
    });
    return drawer;
  }
#endif

  void OnPostSetup() override {
    RestoreXenosAdapter();
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    // The overlay stays on screen while the game draws nothing (the title screen holds the boot).
    gpu::StartIdlePresenter();
#endif
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

    // 7 (rebindable in PC Options > Cheats or Shortcut Keys): free camera on/off.
    rex::ui::RegisterBind("bind_free_camera", "7", "Toggle free camera", [this]() {
      if (options_ && options_->IsCapturing()) return;
      ToggleFreeCamera();
      ShowToast(FreeCameraOn() ? "FREE CAMERA: ON" : "FREE CAMERA: OFF");
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
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    gpu::StopIdlePresenter();
    gpu::overlay::SetFrameRequest(nullptr);
#endif
    rex::ui::UnregisterBind("bind_exit_game");
    rex::ui::UnregisterBind("bind_options");
    rex::ui::UnregisterBind("bind_fps_counter");
    rex::ui::UnregisterBind("bind_free_camera");
    rex::ui::UnregisterBind("bind_toggle_logs");
    for (const auto& sc : ShortcutList()) {
      if (sc.page >= 0) rex::ui::UnregisterBind(sc.cvar);
    }
    if (options_) {
      options_->Save();
    }
    RestoreXenosAdapter();
  }

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  bool OnWindowCloseRequested() override {
    // The native renderer closes in order (guest threads joined, device
    // owner, resources and engine released) before OnClosing hard-exits.
    gpu::StopIdlePresenter();
    return gpu::host::CloseRequested();
  }
#endif

 private:
  static std::filesystem::path ConfigPath() {
    return rex::filesystem::GetExecutableFolder() / "naruto.toml";
  }

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  // UI thread: one frame of the overlay for the renderer's next present (an
  // empty frame when no dialog is open).
  void DrawOverlayFrame() {
    gpu::overlay::FrameRecorded();
    auto* drawer = imgui_drawer();
    if (!drawer || !window() || !overlay_drawer_) return;
    const uint32_t width = window()->GetActualPhysicalWidth();
    const uint32_t height = window()->GetActualPhysicalHeight();
    if (!width || !height) return;
    rex::ui::AppUIDrawContext context(width, height);
    drawer->Draw(context);
    if (!overlay_drawer_->TakeRecorded()) gpu::overlay::Publish(nullptr);
  }
#endif

  // Back + Start on a controller opens or closes the PC options menu.
  void PollControllerShortcut() {
    // Also the per-frame place for the aspect ratio setting (PC Options > Display).
    UpdateUltrawide(runtime() ? runtime()->virtual_membase() : nullptr);
    UpdateMouseCamera();
    UpdateGameSpeed();
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

  OverlayDrawer* overlay_drawer_ = nullptr;  // owned by ReXApp
  OptionsMenu* options_ = nullptr;  // owned by the ImGui drawer; deletes itself on close
  FpsCounter* fps_counter_ = nullptr;
  TitleScreen* title_ = nullptr;  // owned by the ImGui drawer; deletes itself on close
  std::atomic<bool> title_open_ = false;  // read from the game's input thread
  bool shortcut_held_ = false;
  std::atomic<bool> options_open_ = false;  // read from the game's input thread
  std::atomic<bool> hold_input_ = false;
};
