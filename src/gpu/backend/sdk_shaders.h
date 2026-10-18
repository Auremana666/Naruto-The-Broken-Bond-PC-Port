#pragma once

// The output effect and FXAA shaders of the SDK (the bilinear filter,
// FidelityFX CAS and FSR1 EASU/RCAS of the presenter, FXAA of the Swap), DXBC
// SM 5.1, copied unchanged (line endings aside) from sdk/src at rexglue c94f5eb.
// SHA-256 prefix of each source file:
//   ui/shaders/bytecode/d3d12_5_1/guest_output_triangle_strip_rect_vs.h 655f84dd4ba6e36c
//   ui/shaders/bytecode/d3d12_5_1/guest_output_bilinear_ps.h 0c15da72dc038eb0
//   ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_cas_sharpen_ps.h e7d6a528ddb429d7
//   ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_cas_resample_ps.h 8955e977968f85e3
//   ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_fsr_easu_ps.h 12019fe4a515059a
//   ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_fsr_rcas_ps.h 2e37228e14022d1b
//   graphics/shaders/bytecode/d3d12_5_1/fxaa_cs.h f9f6824711ca8039
//   graphics/shaders/bytecode/d3d12_5_1/fxaa_extreme_cs.h acacaa568a6e9e69
#include <windows.h>

namespace gpu::sdk_shaders {
#include "gpu/backend/sdk_shaders/guest_output_triangle_strip_rect_vs.h"
#include "gpu/backend/sdk_shaders/guest_output_bilinear_ps.h"
#include "gpu/backend/sdk_shaders/guest_output_ffx_cas_sharpen_ps.h"
#include "gpu/backend/sdk_shaders/guest_output_ffx_cas_resample_ps.h"
#include "gpu/backend/sdk_shaders/guest_output_ffx_fsr_easu_ps.h"
#include "gpu/backend/sdk_shaders/guest_output_ffx_fsr_rcas_ps.h"
#include "gpu/backend/sdk_shaders/fxaa_cs.h"
#include "gpu/backend/sdk_shaders/fxaa_extreme_cs.h"
}  // namespace gpu::sdk_shaders
