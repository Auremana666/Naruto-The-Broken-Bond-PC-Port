// Two halves from one indexed instanced draw. The offset comes from a
// per-instance stream, and the color also checks the shader's instance ID.
struct Vertex { float4 position : SV_Position; nointerpolation uint instance : TEXCOORD0; };
Vertex vs_main([[vk::location(0)]] float2 position : POSITION0,
               [[vk::location(4)]] float offset : TEXCOORD0, uint instance : SV_InstanceID) {
  Vertex v;
  v.position = float4(position.x + offset, position.y, 0, 1);
  v.instance = instance;
  return v;
}
float4 ps_main(Vertex v) : SV_Target {
  return v.instance == 0 ? float4(1, 0, 0, 1) : float4(0, 1, 0, 1);
}
