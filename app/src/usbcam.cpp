// Xbox Live Vision camera (XUsbcam*) imports, which the SDK does not export.
// Report that no camera is attached so the game skips its camera features.

#include <rex/hook.h>
#include <rex/types.h>

namespace narutobb {

constexpr rex::u32 kErrorDeviceNotConnected = 0x48F;

rex::u32 XUsbcamCreate_entry(rex::u32, rex::u32, mapped_u32 handle) {
  handle.Zero();
  return kErrorDeviceNotConnected;
}
rex::u32 XUsbcamDestroy_entry(rex::u32) { return 0; }
rex::u32 XUsbcamGetState_entry() { return 0; }  // not connected
rex::u32 XUsbcamFail_entry() { return kErrorDeviceNotConnected; }

}  // namespace narutobb

REX_EXPORT(__imp__XUsbcamCreate, narutobb::XUsbcamCreate_entry)
REX_EXPORT(__imp__XUsbcamDestroy, narutobb::XUsbcamDestroy_entry)
REX_EXPORT(__imp__XUsbcamGetState, narutobb::XUsbcamGetState_entry)
REX_EXPORT(__imp__XUsbcamSetConfig, narutobb::XUsbcamFail_entry)
REX_EXPORT(__imp__XUsbcamSetView, narutobb::XUsbcamFail_entry)
REX_EXPORT(__imp__XUsbcamSetCaptureMode, narutobb::XUsbcamFail_entry)
REX_EXPORT(__imp__XUsbcamReadFrame, narutobb::XUsbcamFail_entry)
