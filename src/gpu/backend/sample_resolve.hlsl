// Color resolve of one sample of an MSAA render target
// (RB_COPY_CONTROL.copy_sample_select k0..k3) by a full-screen pass into the
// R8G8B8A8_UNORM destination, as the Xenos copies it: no other sample enters
// the texel. The averaged resolves stay on ResolveSubresource. The vertex
// shader is the full-screen triangle of backend/depth_resolve.hlsl, with the
// same root signature (t0, b0).
// Compiled offline into backend/sample_resolve.h:
// dxc -T ps_6_0 -E ps_main -Vn kSampleResolvePs -Fh ps.h
// The SPIR-V of the Vulkan backend comes from the build
// (tools/native-renderer/build-pass-spirv.py), with the layout of
// backend/depth_resolve.hlsl: the source in set 0, the sample as a push constant.
[[vk::binding(0, 0)]] Texture2DMS<float4> source : register(t0);
#ifdef __spirv__
struct PassConstants {
  uint sample_index;
};
[[vk::push_constant]] PassConstants pass_constants;
#define sample_index pass_constants.sample_index
#else
cbuffer Pass : register(b0) {
  uint sample_index;
};
#endif

float4 ps_main(float4 position : SV_Position) : SV_Target {
  return source.Load(int2(position.xy), sample_index);
}
