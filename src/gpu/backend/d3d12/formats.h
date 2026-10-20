// The D3D12 values of the engine's contracts (backend/contracts.h): formats,
// component mappings, sampler filters and address modes, and topologies.
// Header-only, so the D3D12 host tests use the same table as the backend.
#pragma once

#include <d3d12.h>
#include "gpu/backend/contracts.h"

namespace gpu::d3d12 {

inline DXGI_FORMAT DxgiFormat(VertexFormat format) {
  switch (format) {
    case VertexFormat::kR32G32B32A32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VertexFormat::kR32G32B32A32Uint: return DXGI_FORMAT_R32G32B32A32_UINT;
    case VertexFormat::kR32G32B32Float: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::kR32G32B32Uint: return DXGI_FORMAT_R32G32B32_UINT;
    case VertexFormat::kR16G16B16A16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case VertexFormat::kR16G16B16A16Unorm: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case VertexFormat::kR16G16B16A16Uint: return DXGI_FORMAT_R16G16B16A16_UINT;
    case VertexFormat::kR16G16B16A16Snorm: return DXGI_FORMAT_R16G16B16A16_SNORM;
    case VertexFormat::kR16G16B16A16Sint: return DXGI_FORMAT_R16G16B16A16_SINT;
    case VertexFormat::kR32G32Float: return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::kR32G32Uint: return DXGI_FORMAT_R32G32_UINT;
    case VertexFormat::kR8G8B8A8Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::kR8G8B8A8Uint: return DXGI_FORMAT_R8G8B8A8_UINT;
    case VertexFormat::kR8G8B8A8Snorm: return DXGI_FORMAT_R8G8B8A8_SNORM;
    case VertexFormat::kR8G8B8A8Sint: return DXGI_FORMAT_R8G8B8A8_SINT;
    case VertexFormat::kR16G16Float: return DXGI_FORMAT_R16G16_FLOAT;
    case VertexFormat::kR16G16Unorm: return DXGI_FORMAT_R16G16_UNORM;
    case VertexFormat::kR16G16Uint: return DXGI_FORMAT_R16G16_UINT;
    case VertexFormat::kR16G16Snorm: return DXGI_FORMAT_R16G16_SNORM;
    case VertexFormat::kR16G16Sint: return DXGI_FORMAT_R16G16_SINT;
    case VertexFormat::kR32Float: return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::kR32Uint: return DXGI_FORMAT_R32_UINT;
    case VertexFormat::kUnknown: break;
  }
  return DXGI_FORMAT_UNKNOWN;
}

inline DXGI_FORMAT DxgiFormat(TextureFormat format) {
  switch (format) {
    case TextureFormat::kR32G32B32A32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case TextureFormat::kR16G16B16A16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::kR32G32Float: return DXGI_FORMAT_R32G32_FLOAT;
    case TextureFormat::kR8G8B8A8Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::kR32Float: return DXGI_FORMAT_R32_FLOAT;
    case TextureFormat::kR8G8Unorm: return DXGI_FORMAT_R8G8_UNORM;
    case TextureFormat::kR8Unorm: return DXGI_FORMAT_R8_UNORM;
    case TextureFormat::kBC1Unorm: return DXGI_FORMAT_BC1_UNORM;
    case TextureFormat::kBC4Unorm: return DXGI_FORMAT_BC4_UNORM;
    case TextureFormat::kBC3Unorm: return DXGI_FORMAT_BC3_UNORM;
    case TextureFormat::kBC5Unorm: return DXGI_FORMAT_BC5_UNORM;
    case TextureFormat::kB5G6R5Unorm: return DXGI_FORMAT_B5G6R5_UNORM;
    case TextureFormat::kUnknown: break;
  }
  return DXGI_FORMAT_UNKNOWN;
}

// The swizzles are the D3D12_SHADER_COMPONENT_MAPPING values, in order.
inline UINT ShaderComponentMapping(const ComponentMapping& mapping) {
  return D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(UINT(mapping[0]), UINT(mapping[1]), UINT(mapping[2]),
                                                  UINT(mapping[3]));
}

inline D3D12_FILTER SamplerFilter(const DrawTexture& texture) {
  if (texture.anisotropic) return D3D12_FILTER_ANISOTROPIC;
  const auto type = [](Filter filter) {
    return filter == Filter::kLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
  };
  return D3D12_ENCODE_BASIC_FILTER(type(texture.min_filter), type(texture.mag_filter), type(texture.mip_filter),
                                   D3D12_FILTER_REDUCTION_TYPE_STANDARD);
}

inline D3D12_TEXTURE_ADDRESS_MODE TextureAddressMode(AddressMode mode) {
  switch (mode) {
    case AddressMode::kWrap: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case AddressMode::kMirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case AddressMode::kClamp: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case AddressMode::kBorder: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case AddressMode::kMirrorOnce: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
  }
  return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

inline D3D_PRIMITIVE_TOPOLOGY PrimitiveTopology(Topology topology) {
  return topology == Topology::kTriangleStrip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
                                              : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

}  // namespace gpu::d3d12
