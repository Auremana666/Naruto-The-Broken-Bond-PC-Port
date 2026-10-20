struct Push { uint64_t vs; uint64_t ps; uint64_t common; };
[[vk::push_constant]] ConstantBuffer<Push> constants;
[[vk::binding(0, 0)]] Texture2D<float4> textures[];
[[vk::binding(0, 3)]] SamplerState samplers[];
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vs_main(uint id : SV_VertexID) {
  Vertex v;
  v.uv = float2((id << 1) & 2, id & 2);
  v.position = float4(v.uv * float2(2, -2) + float2(-1, 1) +
                     vk::RawBufferLoad<float4>(constants.vs).xy, 0, 1);
  return v;
}
float4 ps_main(Vertex v) : SV_Target {
  uint image = vk::RawBufferLoad<uint>(constants.common);
  uint sampler_index = vk::RawBufferLoad<uint>(constants.common + 192);
  return textures[image].SampleLevel(samplers[sampler_index], v.uv, 0) *
         vk::RawBufferLoad<float4>(constants.ps);
}
