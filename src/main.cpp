// broken_bond - ReXGlue Recompiled Project

#include "generated/default/broken_bond_init.h"

#include "broken_bond_app.h"

REXCVAR_DEFINE_BOOL(show_fps_overlay, false, "broken_bond/UI",
                    "Show FPS and frametime overlay (toggle with F1, Xenos only)");

REX_DEFINE_APP(broken_bond, BrokenBondApp::Create)
