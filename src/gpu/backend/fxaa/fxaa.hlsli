// The FXAA of the output (SwapPostEffect): src/xenia/gpu/shaders/fxaa.hlsli of
// the Xenia project at 95a5c3ee250f80c3b9d139658649d9ffb6db3eec (LICENSE-xenia),
// around NVIDIA FXAA 3.11 (FXAA3_11.h, LICENSE), the source of the DXBC of
// backend/sdk_shaders/fxaa_cs.h and fxaa_extreme_cs.h: FXC with the arguments of
// that project's build (/D SHADING_LANGUAGE_HLSL_XE=1 /T cs_5_1) gives those
// bytes from this copy. Changes here: FXAA3_11.h from this folder, and the
// Vulkan bindings of the SPIR-V the build compiles
// (tools/native-renderer/build-pass-spirv.py): the constants as push constants
// (root constants in D3D12); the destination (R10G10B10A2, as in the D3D12
// path), the source and the sampler in set 0.

// Inputs:
// - FXAA_QUALITY__PRESET
// - FXAA_QUALITY__SUBPIX
// - FXAA_QUALITY__EDGE_THRESHOLD
// - FXAA_QUALITY__EDGE_THRESHOLD_MIN

// "use of potentially uninitialized variable" in FxaaPixelShader due to the
// early exit with a `return` in the beginning of the function, caused by a bug
// in FXC (the warning is common for early exiting in HLSL in general).
#pragma warning(disable: 4000)

#ifdef __spirv__
struct XeApplyGammaRampConstants {
  uint2 xe_fxaa_size;
  float2 xe_fxaa_size_inv;
};
[[vk::push_constant]] XeApplyGammaRampConstants xe_fxaa_constants;
#define xe_fxaa_size xe_fxaa_constants.xe_fxaa_size
#define xe_fxaa_size_inv xe_fxaa_constants.xe_fxaa_size_inv

[[vk::binding(0, 0)]] [[vk::image_format("rgb10a2")]] RWTexture2D<unorm float4> xe_fxaa_dest;
[[vk::binding(1, 0)]] Texture2D<float4> xe_fxaa_source;
[[vk::binding(2, 0)]] SamplerState xe_sampler_linear_clamp;
#else
cbuffer XeApplyGammaRampConstants : register(b0) {
  uint2 xe_fxaa_size;
  float2 xe_fxaa_size_inv;
};

RWTexture2D<unorm float4> xe_fxaa_dest : register(u0);
Texture2D<float4> xe_fxaa_source : register(t0);
SamplerState xe_sampler_linear_clamp : register(s0);
#endif

#define FXAA_PC 1
#define FXAA_HLSL_5 1
#include "FXAA3_11.h"

[numthreads(16, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  [branch] if (any(xe_thread_id.xy >= xe_fxaa_size)) {
    return;
  }
  FxaaTex fxaa_texture;
  fxaa_texture.smpl = xe_sampler_linear_clamp;
  fxaa_texture.tex = xe_fxaa_source;
  // Force alpha to 1 to simplify calculations, won't need it anymore anyway.
  xe_fxaa_dest[xe_thread_id.xy] =
      float4(
          FxaaPixelShader(
              (float2(xe_thread_id.xy) + 0.5f) * xe_fxaa_size_inv,
              (float2(xe_thread_id.xy).xyxy + float2(0.0f, 1.0f).xxyy) *
                  xe_fxaa_size_inv.xyxy,
              fxaa_texture, fxaa_texture, fxaa_texture, xe_fxaa_size_inv,
              float2(-0.5f, 0.5f).xxyy * xe_fxaa_size_inv.xyxy,
              float2(-2.0f, 2.0f).xxyy * xe_fxaa_size_inv.xyxy,
              float2(8.0f, -4.0f).xxyy * xe_fxaa_size_inv.xyxy,
              FXAA_QUALITY__SUBPIX, FXAA_QUALITY__EDGE_THRESHOLD,
              FXAA_QUALITY__EDGE_THRESHOLD_MIN, 8.0f, 0.125f, 0.05f,
              float4(1.0f, -1.0f, 0.25f, -0.25f)).rgb,
          1.0f);
}
