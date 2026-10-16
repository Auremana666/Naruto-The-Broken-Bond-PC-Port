// broken_bond - ReXGlue Recompiled Project
//
// Minimal host for the recompiled game using the SDK runtime.

#pragma once

#include <rex/rex_app.h>

class BrokenBondApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BrokenBondApp>(new BrokenBondApp(ctx, "broken_bond",
        PPCImageConfig));
  }

};
