#pragma once

// The D3D boundary of the native renderer seen from the host.
#include <memory>
#include <string>
#include <string_view>
#include <rex/ppc/context.h>
#include "gpu/host/services.h"

// Entry points of the D3D boundary that the host calls. Hooks owned only by
// the native mode live in the units of the boundary; hooks shared with the
// earlier altitude (host/host.cpp) call these first and
// return when Active().
namespace gpu {
bool Active();
void DeviceArena(PPCContext& ctx, uint8_t* base);        // 8219FEB8 inside 821B4060
void DeviceWorkers(PPCContext& ctx, uint8_t* base);      // 821B8D78
void DeviceCache(PPCContext& ctx, uint8_t* base);        // 821A5498
void DeviceInitializer(PPCContext& ctx, uint8_t* base);  // 821B7570
void DeviceVideo(PPCContext& ctx, uint8_t* base);        // 821B3BD8
void DeviceDefaults(PPCContext& ctx, uint8_t* base);     // 821B38F8
void DeviceMemory(PPCContext& ctx, uint8_t* base);       // 821F6290 at LR 821B41C8, runs the original
void RingReserve(PPCContext& ctx, uint8_t* base);        // 8219F890: flush omitted or guard
bool AdmitExport(std::string_view name, PPCContext& ctx);
void ExportAfter(std::string_view name, PPCContext& ctx);  // Records admitted queries.
// True when a packet-writing leaf (8219FA88) runs inside an own discard scope
// of this thread with its cursor in the discard buffer.
bool AdmitDiscardLeaf(PPCContext& ctx);
// True when 8219F890 is the space reserve of the 82170540 tail (LR 82170FBC)
// inside its own scope; r3 then holds the own discard buffer.
bool AdmitSceneFlushReserve(PPCContext& ctx, uint8_t* base);
// True when 8219F890 is the ring kick at the end of 821BD008 (LR 821BD1B0)
// or 821B1BF0 (LR 821B1DC4) inside the discard scope of the own Swap; r3
// then holds +48.
bool AdmitSwapDiscardKick(PPCContext& ctx, uint8_t* base);
// Asks the render thread to stop in order (StopInOrder) at its next
// own Swap or draw. False when the native mode has no live device.
bool RequestStop(std::string_view reason, int code);
// The own interrupt source flipped the frame of this fence; publishes
// it as the completed fence (DeviceOwner::PublishFrame).
bool PublishFrameFence(uint32_t fence);
// Posts the Present of a frame at its flip, before its fence is
// published. False when it could not be posted.
bool PresentFrame(const std::shared_ptr<host::FramePresent>& present);
std::string PresentSummary();
// REX_NATIVE_PRESENT_PROOF=1 proves the own swapchain on the SDK
// window right after the engine is ready, before any title frame, and stops.
bool PresentProofRequested();
[[noreturn]] void PresentProof(PPCContext& ctx);
// The ordered close on the window close (UI thread). The render
// thread parks at its next own Swap or draw (10 s, else the fallback claims the
// stop and the render thread parks behind it); then the own interrupt thread
// is joined, the title terminated and its guest threads joined, and the device
// owner, the registry resources, the swapchain and the engine are released.
bool CloseInOrder();
}  // namespace gpu
