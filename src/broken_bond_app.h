// broken_bond - ReXGlue Recompiled Project
//
// Minimal host for the recompiled game using the SDK runtime.

#pragma once

#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/ui/keybinds.h>

#include "fps_overlay.h"
#include "graphics_device.h"
#include "input_replay.h"
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
#include "gpu/host/app.h"
#endif

class BrokenBondApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BrokenBondApp>(new BrokenBondApp(ctx, "broken_bond",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // Input recording and replay for test boots (input_replay.cpp); the SDK's
    // default input system when neither is asked for.
    config.input_factory = CreateInputSystem;
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    // The native renderer replaces the Xenos plugin when renderer = "native".
    gpu::host::Configure(config);
#endif
    if (!config.graphics) SelectXenosAdapter();
  }

  void OnPostSetup() override { RestoreXenosAdapter(); }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    SetGuestFrameStats(ReadGuestFrameStats);
    if (rex::cvar::Query<bool>("show_fps_overlay")) {
      fps_overlay_ = std::make_unique<FpsOverlayDialog>(drawer, ReadGuestFrameStats);
    }
    rex::ui::RegisterBind("bind_fps_overlay", "F1", "Toggle FPS overlay", [this] {
      if (fps_overlay_) {
        fps_overlay_.reset();
      } else if (imgui_drawer()) {
        fps_overlay_ = std::make_unique<FpsOverlayDialog>(imgui_drawer(), ReadGuestFrameStats);
      }
      rex::cvar::SetFlagByName("show_fps_overlay", fps_overlay_ ? "true" : "false");
    });
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_fps_overlay");
    fps_overlay_.reset();
    RestoreXenosAdapter();
  }

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  bool OnWindowCloseRequested() override {
    // The native renderer closes in order (guest threads joined, device
    // owner, resources and engine released) before OnClosing hard-exits.
    return gpu::host::CloseRequested();
  }
#endif

 private:
  std::unique_ptr<FpsOverlayDialog> fps_overlay_;
};
