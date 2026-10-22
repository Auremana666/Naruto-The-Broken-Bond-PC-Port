// Ultrawide (PC Options > Display > Aspect Ratio), the way the Naruto: Rise of a Ninja port does it
// (naruto-rise-recomp, GPL-3.0): the game's camera keeps a 9/16 constant for the width of its view;
// replacing it with 1/aspect makes the game render a wider view squeezed into its 16:9 frame, and
// the SDK's presenter (ultrawide_target_aspect, see the SDK patch) stretches that frame back out
// to the wider shape. The HUD is stretched along with it.

#include "ultrawide.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>

#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
#include "gpu/overlay.h"
#endif

REXCVAR_DEFINE_STRING(aspect_ratio, "16:9", "Display",
                      "Aspect ratio of the picture: 16:9, 21:9, 32:9 or auto (the screen's)")
    .allowed({"16:9", "21:9", "32:9", "auto"})
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(field_of_view, 1.0, "Display",
                      "Factor on the camera's field of view (1 = the game's own, up to 1.5)");
REXCVAR_DEFINE_BOOL(ultrawide_probe, false, "Display",
                    "Log shader constants that look like a 16:9 projection (diagnostics)");
REXCVAR_DEFINE_DOUBLE(ultrawide_target_aspect, 0.0, "Display",
                      "Set by the game from aspect_ratio (0 = 16:9); read by the presenter");

namespace {

constexpr float kNativeAspect = 16.0f / 9.0f;
// The camera's 9/16 view-width value in Broken Bond's data (found by changing every 9/16 in memory
// in the open world and watching the view widen; Rise of a Ninja keeps it at 0x826F5108).
constexpr uint32_t kViewWidthFactorAddress = 0x82E26800;
constexpr float kOriginalFactor = 9.0f / 16.0f;

float g_applied_factor = kOriginalFactor;

uint32_t LoadU32(const uint8_t* base, uint32_t address) {
  uint32_t raw;
  std::memcpy(&raw, base + address, sizeof(raw));
  return std::byteswap(raw);
}

void StoreU32(uint8_t* base, uint32_t address, uint32_t value) {
  const uint32_t raw = std::byteswap(value);
  std::memcpy(base + address, &raw, sizeof(raw));
}

double TargetAspect() {
  const std::string mode = REXCVAR_GET(aspect_ratio);
  if (mode == "21:9") return 21.5 / 9.0;  // 3440x1440 and 2560x1080 screens
  if (mode == "32:9") return 32.0 / 9.0;
  if (mode == "auto") {
    const double w = GetSystemMetrics(SM_CXSCREEN);
    const double h = GetSystemMetrics(SM_CYSCREEN);
    if (h > 0 && w / h > 1.85) return w / h;
  }
  return kNativeAspect;
}

}  // namespace

void UpdateUltrawide(uint8_t* base) {
  if (!base) return;
  const double aspect = TargetAspect();
  const bool wide = aspect > kNativeAspect + 0.05;
  const float factor = wide ? static_cast<float>(1.0 / aspect) : kOriginalFactor;
  const double presenter_aspect = wide ? aspect : 0.0;
  if (REXCVAR_GET(ultrawide_target_aspect) != presenter_aspect) {
    REXCVAR_SET(ultrawide_target_aspect, presenter_aspect);
#ifdef BROKENBOND_NATIVE_RENDERER_BOOTSTRAP
    gpu::overlay::SetTargetAspect(presenter_aspect);  // The native renderer's present.
#endif
  }
  // Checked every frame: the value lives in the game's data, so it is put back if it changes.
  const uint32_t current = LoadU32(base, kViewWidthFactorAddress);
  if (current == std::bit_cast<uint32_t>(factor)) return;
  if (current != std::bit_cast<uint32_t>(kOriginalFactor) &&
      current != std::bit_cast<uint32_t>(g_applied_factor)) {
    static bool reported = false;
    if (!reported) {
      REXLOG_ERROR("[ultrawide] unexpected value {:08X} at {:08X}; not changing it", current,
                   kViewWidthFactorAddress);
      reported = true;
    }
    return;
  }
  void* where = base + kViewWidthFactorAddress;
  rex::memory::PageAccess old_access{};
  if (!rex::memory::Protect(where, sizeof(uint32_t), rex::memory::PageAccess::kReadWrite,
                            &old_access)) {
    REXLOG_ERROR("[ultrawide] cannot make the camera value writable");
    return;
  }
  StoreU32(base, kViewWidthFactorAddress, std::bit_cast<uint32_t>(factor));
  rex::memory::Protect(where, sizeof(uint32_t), old_access);
  g_applied_factor = factor;
  REXLOG_INFO("[ultrawide] aspect {:.4f}, view width factor {:.6f}", wide ? aspect : kNativeAspect,
              factor);
}

float FieldOfViewScale() {
  const double scale = REXCVAR_GET(field_of_view);
  return scale > 0.5 && scale < 1.6 ? static_cast<float>(scale) : 1.0f;
}
