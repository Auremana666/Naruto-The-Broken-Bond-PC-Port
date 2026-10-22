#include "gpu/backend/d3d12/overlay_renderer.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gpu::d3d12 {
namespace {
constexpr char kShader[] = R"(
cbuffer Constants : register(b0) { float2 scale; uint textured; uint point_filter; };
Texture2D image : register(t0);
SamplerState linear_sampler : register(s0);
SamplerState point_sampler : register(s1);
struct VsIn { float2 position : POSITION; float2 uv : TEXCOORD; float4 color : COLOR; };
struct PsIn { float4 position : SV_Position; float2 uv : TEXCOORD; float4 color : COLOR; };
PsIn vs(VsIn i) {
  PsIn o;
  o.position = float4(i.position.x * scale.x - 1.0, 1.0 - i.position.y * scale.y, 0.0, 1.0);
  o.uv = i.uv;
  o.color = i.color;
  return o;
}
float4 ps(PsIn i) : SV_Target {
  float4 color = i.color;
  if (textured) color *= point_filter ? image.Sample(point_sampler, i.uv) : image.Sample(linear_sampler, i.uv);
  return color;
}
)";

bool Compile(const char* entry, const char* target, Microsoft::WRL::ComPtr<ID3DBlob>& blob) {
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  return SUCCEEDED(D3DCompile(kShader, sizeof(kShader) - 1, "overlay", nullptr, nullptr, entry, target,
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors));
}
}  // namespace

bool OverlayRenderer::Initialize(ID3D12Device* device) {
  Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
  if (!Compile("vs", "vs_5_0", vs) || !Compile("ps", "ps_5_0", ps)) return false;

  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameters[2]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.Num32BitValues = 4;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &range;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samplers[2]{};
  for (int i = 0; i < 2; ++i) {
    samplers[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[i].ShaderRegister = i;
    samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  D3D12_ROOT_SIGNATURE_DESC root{};
  root.NumParameters = 2;
  root.pParameters = parameters;
  root.NumStaticSamplers = 2;
  root.pStaticSamplers = samplers;
  root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  Microsoft::WRL::ComPtr<ID3DBlob> serialized, errors;
  if (FAILED(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors)) ||
      FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                         IID_PPV_ARGS(&root_))))
    return false;

  const D3D12_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
  desc.pRootSignature = root_.Get();
  desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
  auto& blend = desc.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
  blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.InputLayout = {layout, 3};
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_)))) return false;

  const D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSlots,
                                        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  if (FAILED(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&heap_)))) return false;
  descriptor_step_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  for (uint32_t i = kSlots; i-- > 0;) free_slots_.push_back(i);
  return true;
}

bool OverlayRenderer::Prepare(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                              const overlay::Texture& texture, GpuTexture& gpu, const UploadFn& upload) {
  if (free_slots_.empty() || !texture.width || !texture.height) return false;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = texture.width;
  desc.Height = texture.height;
  desc.DepthOrArraySize = desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&gpu.resource))))
    return false;
  const uint32_t pitch = (texture.width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                         ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
  Upload memory;
  if (!upload(size_t(pitch) * texture.height, memory)) return false;
  for (uint32_t y = 0; y < texture.height; ++y)
    std::memcpy(memory.cpu + size_t(y) * pitch, texture.rgba.data() + size_t(y) * texture.width * 4,
                texture.width * 4);
  D3D12_TEXTURE_COPY_LOCATION to{}, from{};
  to.pResource = gpu.resource.Get();
  to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  from.pResource = memory.buffer;
  from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  from.PlacedFootprint.Offset = memory.offset;
  from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, texture.width, texture.height, 1, pitch};
  commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = gpu.resource.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  commands->ResourceBarrier(1, &barrier);
  gpu.slot = free_slots_.back();
  free_slots_.pop_back();
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  auto cpu = heap_->GetCPUDescriptorHandleForHeapStart();
  cpu.ptr += SIZE_T(descriptor_step_) * gpu.slot;
  device->CreateShaderResourceView(gpu.resource.Get(), &srv, cpu);
  return true;
}

bool OverlayRenderer::Draw(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                           D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t width, uint32_t height,
                           const UploadFn& upload, const RetainFn& retain) {
  ++frames_;
  // Textures no frame refers to anymore; their slots wait out the frames in flight.
  for (auto it = textures_.begin(); it != textures_.end();) {
    if (it->second.source.expired()) {
      retain(it->second.resource);
      released_slots_.push_back({it->second.slot, frames_});
      it = textures_.erase(it);
    } else {
      ++it;
    }
  }
  std::erase_if(released_slots_, [&](const auto& released) {
    if (frames_ - released.second < 8) return false;
    free_slots_.push_back(released.first);
    return true;
  });

  const auto frame = overlay::Latest();
  if (!frame || frame->commands.empty() || frame->indices.empty() || failed_) return true;
  if (!initialized_) {
    initialized_ = true;
    if (!Initialize(device)) {
      failed_ = true;
      return true;  // No overlay; the game keeps running.
    }
  }

  Upload vertices, indices;
  const size_t vertex_bytes = frame->vertices.size() * sizeof(overlay::Vertex);
  const size_t index_bytes = frame->indices.size() * sizeof(uint32_t);
  if (!upload(vertex_bytes, vertices) || !upload(index_bytes, indices)) return false;
  std::memcpy(vertices.cpu, frame->vertices.data(), vertex_bytes);
  std::memcpy(indices.cpu, frame->indices.data(), index_bytes);

  // New textures first: their copies run before the draws in the same list.
  for (const auto& command : frame->commands) {
    if (!command.texture || textures_.count(command.texture.get())) continue;
    GpuTexture gpu;
    gpu.source = command.texture;
    if (!Prepare(device, commands, *command.texture, gpu, upload)) continue;
    textures_.emplace(command.texture.get(), std::move(gpu));
  }

  commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  const D3D12_VIEWPORT viewport{0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
  commands->RSSetViewports(1, &viewport);
  ID3D12DescriptorHeap* heaps[] = {heap_.Get()};
  commands->SetDescriptorHeaps(1, heaps);
  commands->SetGraphicsRootSignature(root_.Get());
  commands->SetPipelineState(pipeline_.Get());
  commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VERTEX_BUFFER_VIEW vbv{vertices.gpu, UINT(vertex_bytes), sizeof(overlay::Vertex)};
  const D3D12_INDEX_BUFFER_VIEW ibv{indices.gpu, UINT(index_bytes), DXGI_FORMAT_R32_UINT};
  commands->IASetVertexBuffers(0, 1, &vbv);
  commands->IASetIndexBuffer(&ibv);
  // A valid table for color-only draws too (the shader declares the texture).
  commands->SetGraphicsRootDescriptorTable(1, heap_->GetGPUDescriptorHandleForHeapStart());

  const float sx = float(width) / frame->width, sy = float(height) / frame->height;
  struct Constants {
    float scale[2];
    uint32_t textured, point_filter;
  } constants{{2.0f / frame->width, 2.0f / frame->height}, 0, 0};
  for (const auto& command : frame->commands) {
    LONG left = 0, top = 0, right = LONG(width), bottom = LONG(height);
    if (command.scissor) {
      left = std::clamp(LONG(std::floor(command.left * sx)), 0L, LONG(width));
      top = std::clamp(LONG(std::floor(command.top * sy)), 0L, LONG(height));
      right = std::clamp(LONG(std::ceil(command.right * sx)), 0L, LONG(width));
      bottom = std::clamp(LONG(std::ceil(command.bottom * sy)), 0L, LONG(height));
    }
    if (right <= left || bottom <= top) continue;
    const D3D12_RECT scissor{left, top, right, bottom};
    commands->RSSetScissorRects(1, &scissor);
    constants.textured = 0;
    if (command.texture) {
      auto it = textures_.find(command.texture.get());
      if (it == textures_.end()) continue;
      constants.textured = 1;
      constants.point_filter = command.texture->linear ? 0 : 1;
      auto gpu = heap_->GetGPUDescriptorHandleForHeapStart();
      gpu.ptr += UINT64(descriptor_step_) * it->second.slot;
      commands->SetGraphicsRootDescriptorTable(1, gpu);
    }
    commands->SetGraphicsRoot32BitConstants(0, 4, &constants, 0);
    commands->DrawIndexedInstanced(command.index_count, 1, command.first_index, 0, 0);
  }
  return true;
}

void OverlayRenderer::Release() {
  textures_.clear();
  released_slots_.clear();
  free_slots_.clear();
  pipeline_.Reset();
  root_.Reset();
  heap_.Reset();
  initialized_ = failed_ = false;
}

}  // namespace gpu::d3d12
