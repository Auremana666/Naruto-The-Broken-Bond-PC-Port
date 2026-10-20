// Depth resolve of the own D3DDevice_Resolve (source: the depth-stencil
// surface) by a full-screen pass. The pixel shader writes the depth of one
// sample of the source (RB_COPY_CONTROL.copy_sample_select k0..k3), or the
// minimum of all samples, into the R32_FLOAT
// destination texture. MSAA selects Texture2DMS; otherwise Texture2D.
// Compiled offline into backend/depth_resolve.h:
// dxc -T vs_6_0 -E vs_main -Vn kNativeDepthResolveVs -Fh vs.h
// dxc -T ps_6_0 -E ps_main -D MSAA=1 -Vn kNativeDepthResolveMsPs -Fh ms.h
// dxc -T ps_6_0 -E ps_main -D MSAA=0 -Vn kNativeDepthResolveSinglePs -Fh single.h
// The SPIR-V of the Vulkan backend comes from the build
// (tools/native-renderer/build-pass-spirv.py): the source in set 0, the pass
// constants (root constants in D3D12) as push constants.
#ifndef MSAA
#define MSAA 0
#endif
#if MSAA
[[vk::binding(0, 0)]] Texture2DMS<float> source : register(t0);
#else
[[vk::binding(0, 0)]] Texture2D<float> source : register(t0);
#endif
#ifdef __spirv__
struct PassConstants {
  int sample_index;
  uint samples;
};
[[vk::push_constant]] PassConstants pass_constants;
#define sample_index pass_constants.sample_index
#define samples pass_constants.samples
#else
cbuffer Pass : register(b0) {
  int sample_index;  // -1: minimum of all samples.
  uint samples;
};
#endif

float4 vs_main(uint id : SV_VertexID) : SV_Position {
  const float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float ps_main(float4 position : SV_Position) : SV_Target {
  const int2 p = int2(position.xy);
#if MSAA
  if (sample_index >= 0) return source.Load(p, sample_index);
  float result = source.Load(p, 0);
  for (uint i = 1; i < samples; ++i) result = min(result, source.Load(p, i));
  return result;
#else
  return source.Load(int3(p, 0));
#endif
}
