// Final pass of the own swapchain (B8G8R8A8_UNORM back buffer). The
// copy of the front buffer (RGBA8) goes through the title's gamma ramp
// (D3DGAMMARAMP of +14988 as 10-bit values per channel), is scaled into the
// image rect and is rounded to 8 bits per channel. The pass covers the whole
// back buffer: outside the image rect it writes the black bars (no clear). The ramp is applied per texel before the bilinear
// filter, as the SDK's presenter applies it to the front buffer before scaling.
// Compiled offline into backend/present_pass.h (dxc -T vs_6_0 or ps_6_0 -E <entry>):
// vs_main as kPresentVs, ps_main as kPresentPs, ps_gamma as kPresentGammaPs and
// ps_gamma_luma as kPresentGammaLumaPs. The SPIR-V of the Vulkan backend comes
// from the build (tools/native-renderer/build-pass-spirv.py): the texture and
// the ramp in set 0, the pass constants (root constants in D3D12) as push
// constants.
[[vk::binding(0, 0)]] Texture2D<float4> source : register(t0);
[[vk::binding(1, 0)]] cbuffer Ramp : register(b0) { uint4 ramp[256]; };  // xyz: 10-bit r, g, b
#ifdef __spirv__
struct PassConstants {
  float2 origin;
  float2 scale;
  uint2 size;
  float2 extent;
};
[[vk::push_constant]] PassConstants pass_constants;
#define origin pass_constants.origin
#define scale pass_constants.scale
#define size pass_constants.size
#define extent pass_constants.extent
#else
cbuffer Pass : register(b1) {
  float2 origin;  // Image rect origin in the back buffer, in pixels.
  float2 scale;   // Source texels per back-buffer pixel.
  uint2 size;     // Source size.
  float2 extent;  // Image rect size, in pixels.
};
#endif

float4 vs_main(uint id : SV_VertexID) : SV_Position {
  const float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 Gamma(float4 c) {
  const uint3 i = uint3(round(saturate(c.rgb) * 255.0));
  return float3(ramp[i.r].x, ramp[i.g].y, ramp[i.b].z);
}

float4 ps_main(float4 position : SV_Position) : SV_Target {
  // At scale 1 the sample point is a texel center and f is 0: the output is
  // the ramp value of that texel, rounded from 10 to 8 bits.
  const float2 local = position.xy - origin;
  if (any(local < 0.0) || any(local >= extent)) return float4(0.0, 0.0, 0.0, 1.0);
  const float2 s = local * scale - 0.5;
  const float2 base = floor(s);
  const float2 f = s - base;
  const int2 hi = int2(size) - 1;
  const int2 p0 = clamp(int2(base), int2(0, 0), hi), p1 = clamp(int2(base) + 1, int2(0, 0), hi);
  const float3 a = Gamma(source.Load(int3(p0.x, p0.y, 0))), b = Gamma(source.Load(int3(p1.x, p0.y, 0)));
  const float3 c = Gamma(source.Load(int3(p0.x, p1.y, 0))), d = Gamma(source.Load(int3(p1.x, p1.y, 0)));
  const float3 v = lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
  return float4(round(v * (255.0 / 1023.0)) / 255.0, 1.0);
}

// The ramp alone, at the source size, before the output effects of the
// SDK (CAS, FSR1, FXAA). The target is R10G10B10A2, as the SDK's guest output;
// with FXAA it is R16G16B16A16 with the luma of the SDK's
// apply_gamma_table_fxaa_luma in alpha.
float3 GammaAt(float2 position) {
  return Gamma(source.Load(int3(int2(position), 0))) * (1.0 / 1023.0);
}
float4 ps_gamma(float4 position : SV_Position) : SV_Target {
  return float4(GammaAt(position.xy), 1.0);
}
float4 ps_gamma_luma(float4 position : SV_Position) : SV_Target {
  const float3 v = GammaAt(position.xy);
  return float4(v, dot(v, float3(0.299, 0.587, 0.114)));
}
