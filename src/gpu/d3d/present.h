#pragma once

// Presentation layer of the D3D boundary (d3d/present.cpp): the
// own swapchain on the SDK window, the title's gamma ramp and the Present of
// each own Swap.
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include <rex/ppc/context.h>
#include "gpu/d3d/d3d.h"

namespace rex::ui { class Window; }

namespace gpu {
rex::ui::Window* DisplayWindow();
HWND DisplayHwnd();
// Window helpers shared with the swapchain proof (diagnostics/present_proof.cpp).
bool InUiThread(std::function<void()> function);
std::pair<uint32_t, uint32_t> ClientSize(HWND hwnd);
bool WaitClient(HWND hwnd, uint32_t w, uint32_t h, int ms);
bool SetClientSize(HWND hwnd, uint32_t w, uint32_t h);
std::string WindowFacts(HWND hwnd);
// A file of REX_NATIVE_FRAME_DIR, as JSON fields (file, sha256).
std::string PresentFile(const std::string& name, const std::vector<uint8_t>& bytes);
// REX_NATIVE_CLOSE_AFTER_SWAPS, the window close without a person.
void MaybePostClose(uint64_t swap);
// First own Swap: the swapchain on the SDK window, before the first frame end.
void AttachPresentation(PPCContext& ctx);
// The PC overlay's frames while the game presents nothing (app start to close).
void StartIdlePresenter();
void StopIdlePresenter();
// The ramp of the title when SetGammaRamp marked it pending (bit 1 of +16708).
void PostGammaRamp(PPCContext& ctx, uint8_t* base, uint32_t dev);
// The output options PC Options may change while the game runs (VSync,
// brightness, upscaling, FXAA): registered once, applied at the next Present.
void RegisterLivePresentOptions();
void ApplyPresentSettings();
// The copy of the front buffer for this Swap's flip, in the frame lot.
std::shared_ptr<host::FramePresent> PostStageFront(PPCContext& ctx, uint32_t front, uint64_t swap,
                                                               bool capture);
}  // namespace gpu
