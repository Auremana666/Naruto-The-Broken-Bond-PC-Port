// broken_bond - ReXGlue Recompiled Project
//
// Minimal host for the recompiled game using the SDK runtime.

#pragma once

#include <rex/rex_app.h>

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
  }

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
  bool OnWindowCloseRequested() override {
    // The native renderer closes in order (guest threads joined, device
    // owner, resources and engine released) before OnClosing hard-exits.
    return gpu::host::CloseRequested();
  }
#endif
};
