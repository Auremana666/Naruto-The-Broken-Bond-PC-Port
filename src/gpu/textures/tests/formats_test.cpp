// Host test of the guest texture formats (textures/formats.cpp)
// against the SDK's own load shaders. For every format of the table, tiled and
// linear, in the four endian modes and at an aligned and an odd size, random
// guest bytes go through the SDK's D3D12 texture load compute shader (the
// bytecode of sdk/src/graphics/shaders/bytecode/d3d12_5_1, run with the root
// signature and the LoadConstants of D3D12TextureCache::
// LoadTextureDataFromResidentMemoryImpl, unscaled) on a WARP device, and through
// ReadGuestLevel and ConvertTexels, the code ReadTextureLevels runs in the
// product; the host texels must be equal byte for byte. Never enters a guest
// function. The table must hold exactly the 14 formats of formats.h
// (Rise's 13 and Broken Bond's k_DXT5A), checked first. Layouts (mip chains, the packed tail, linear
// mips, cube faces) go through GetTextureLayout (layout.cpp) and
// ReadGuestLevel, against the SDK's load procedure: each stored level and
// slice loaded by its load shader with the constants of
// LoadTextureDataFromResidentMemoryImpl, and each level of the packed tail
// taken at its GetPackedMipOffset block offset.
// Usage: native_texture_formats_test <new output.jsonl>
#include "gpu/textures/formats.h"
#include "gpu/textures/layout.h"
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <bit>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace sdk_shaders {
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_8bpb_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_32bpb_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_64bpb_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_128bpb_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_r5g6b5_b5g6r5_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_depth_unorm_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_depth_float_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_dxt1_rgba8_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_dxt5_rgba8_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_dxn_rg8_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_dxt5a_r8_cs.h"
#include "graphics/shaders/bytecode/d3d12_5_1/texture_load_ctx1_cs.h"
}  // namespace sdk_shaders

using Microsoft::WRL::ComPtr;
using namespace gpu;

namespace {
// A load shader of the SDK and its LoadShaderInfo (sdk/src/graphics/pipeline/
// texture/cache.cpp, load_shader_info_): source and destination element size
// (log2 bytes), host bytes per block and guest blocks per thread along X (log2).
struct LoadShader {
  const char* name;
  const BYTE* code;
  size_t size;
  uint32_t source_bpe_log2, dest_bpe_log2, bytes_per_host_block, x_blocks_per_thread_log2;
};
const LoadShader k8bpb{"8bpb", sdk_shaders::texture_load_8bpb_cs, sizeof(sdk_shaders::texture_load_8bpb_cs), 3, 4, 1, 4};
const LoadShader k32bpb{"32bpb", sdk_shaders::texture_load_32bpb_cs, sizeof(sdk_shaders::texture_load_32bpb_cs), 4, 4, 4, 3};
const LoadShader k64bpb{"64bpb", sdk_shaders::texture_load_64bpb_cs, sizeof(sdk_shaders::texture_load_64bpb_cs), 4, 4, 8, 2};
const LoadShader k128bpb{"128bpb", sdk_shaders::texture_load_128bpb_cs, sizeof(sdk_shaders::texture_load_128bpb_cs), 4, 4, 16, 1};
const LoadShader kR5G6B5{"r5g6b5_b5g6r5", sdk_shaders::texture_load_r5g6b5_b5g6r5_cs,
                         sizeof(sdk_shaders::texture_load_r5g6b5_b5g6r5_cs), 4, 4, 2, 4};
#define SHADER(name, ...) {#name, sdk_shaders::texture_load_##name##_cs, sizeof(sdk_shaders::texture_load_##name##_cs), __VA_ARGS__}
const LoadShader kDepthUnorm SHADER(depth_unorm, 4, 4, 4, 3);
const LoadShader kDepthFloat SHADER(depth_float, 4, 4, 4, 3);
// The decompressing load shaders (host bytes per texel).
const LoadShader kDXT1 SHADER(dxt1_rgba8, 4, 4, 4, 2);
const LoadShader kDXT5 SHADER(dxt5_rgba8, 4, 4, 4, 1);
const LoadShader kDXN SHADER(dxn_rg8, 4, 4, 2, 1);
const LoadShader kDXT5A SHADER(dxt5a_r8, 4, 4, 1, 2);
const LoadShader kCTX1 SHADER(ctx1, 4, 4, 2, 2);
#undef SHADER
// The load shader of each format, as the SDK's host_formats_ names it
// (sdk/src/graphics/d3d12/texture_cache.cpp).
const LoadShader* ShaderOf(uint32_t format, bool decompressed) {
  if (decompressed) switch (format) {
      case 18: return &kDXT1;
      case 20: return &kDXT5;
      case 49: return &kDXN;
      case 59: return &kDXT5A;
      default: return nullptr;
    }
  switch (format) {
    case 60: return &kCTX1;                      // k_CTX1
    case 2: return &k8bpb;                       // k_8
    case 4: return &kR5G6B5;                     // k_5_6_5
    case 22: return &kDepthUnorm;                // k_24_8
    case 23: return &kDepthFloat;                // k_24_8_FLOAT
    case 6: case 36: return &k32bpb;             // k_8_8_8_8, k_32_FLOAT
    case 18: case 32: case 37: case 59: return &k64bpb;  // k_DXT1, k_16_16_16_16_FLOAT, k_32_32_FLOAT, k_DXT5A
    case 20: case 38: case 49: return &k128bpb;  // k_DXT4_5, k_32_32_32_32_FLOAT, k_DXN
    default: return nullptr;
  }
}

constexpr uint32_t kGuestOffset = 0x3000, kSourceBytes = 1u << 20, kDestBytes = 1u << 20;
uint32_t Align(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

struct Gpu {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  uint64_t fence_value = 0;
  HANDLE event = nullptr;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12DescriptorHeap> heap;
  ComPtr<ID3D12Resource> source, dest, readback;
  std::map<const LoadShader*, ComPtr<ID3D12PipelineState>> pipelines;
  std::string error;

  ComPtr<ID3D12Resource> Buffer(D3D12_HEAP_TYPE type, uint32_t size, D3D12_RESOURCE_FLAGS flags,
                                D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES props{};
    props.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> out;
    device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&out));
    return out;
  }
  bool Init() {
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
      error = "warp-device";
      return false;
    }
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list));
    list->Close();
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    // The SDK's load root signature: 0 the constants, 1 the source SRV, 2 the destination UAV.
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 10;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = {1, &ranges[0]};
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable = {1, &ranges[1]};
    D3D12_ROOT_SIGNATURE_DESC root_desc{3, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errors;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
        FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)))) {
      error = "root-signature";
      return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2,
                                         D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));
    source = Buffer(D3D12_HEAP_TYPE_UPLOAD, kSourceBytes, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    dest = Buffer(D3D12_HEAP_TYPE_DEFAULT, kDestBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    readback = Buffer(D3D12_HEAP_TYPE_READBACK, kDestBytes, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!heap || !source || !dest || !readback) {
      error = "resources";
      return false;
    }
    return true;
  }
  ID3D12PipelineState* Pipeline(const LoadShader& shader) {
    auto& pipeline = pipelines[&shader];
    if (!pipeline) {
      D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
      desc.pRootSignature = root.Get();
      desc.CS = {shader.code, shader.size};
      device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline));
    }
    return pipeline.Get();
  }
  // One SDK load of a level at kGuestOffset of the source buffer; host rows of host_pitch bytes.
  bool Load(const LoadShader& shader, const uint32_t constants[10], uint32_t groups_x, uint32_t groups_y,
            uint32_t bytes, std::vector<uint8_t>& out) {
    ID3D12PipelineState* pipeline = Pipeline(shader);
    if (!pipeline) {
      error = std::string("pipeline-") + shader.name;
      return false;
    }
    const UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    const DXGI_FORMAT pow2[] = {DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R32_UINT,
                                DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32B32A32_UINT};
    srv.Format = pow2[shader.source_bpe_log2];
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = kSourceBytes >> shader.source_bpe_log2;
    device->CreateShaderResourceView(source.Get(), &srv, cpu);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = pow2[shader.dest_bpe_log2];
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = kDestBytes >> shader.dest_bpe_log2;
    device->CreateUnorderedAccessView(dest.Get(), nullptr, &uav, {cpu.ptr + increment});
    allocator->Reset();
    list->Reset(allocator.Get(), pipeline);
    ID3D12DescriptorHeap* heaps[] = {heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root.Get());
    list->SetComputeRoot32BitConstants(0, 10, constants, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
    list->SetComputeRootDescriptorTable(1, gpu);
    list->SetComputeRootDescriptorTable(2, {gpu.ptr + increment});
    list->Dispatch(groups_x, groups_y, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dest.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    list->CopyBufferRegion(readback.Get(), 0, dest.Get(), 0, bytes);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    list->Close();
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.Get(), ++fence_value);
    fence->SetEventOnCompletion(fence_value, event);
    WaitForSingleObject(event, INFINITE);
    if (FAILED(device->GetDeviceRemovedReason())) {
      error = "device-removed";
      return false;
    }
    void* mapped = nullptr;
    D3D12_RANGE range{0, bytes};
    if (FAILED(readback->Map(0, &range, &mapped))) {
      error = "readback-map";
      return false;
    }
    out.assign(static_cast<uint8_t*>(mapped), static_cast<uint8_t*>(mapped) + bytes);
    D3D12_RANGE none{0, 0};
    readback->Unmap(0, &none);
    return true;
  }
};
}  // namespace

// The layout cases.
uint32_t LayoutCases(Gpu& gpu, const std::vector<uint8_t>& guest, std::ofstream& out, uint32_t& cases) {
  namespace xenos = rex::graphics::xenos;
  namespace texture_util = rex::graphics::texture_util;
  struct Case {
    uint32_t format, width, height, mip_max;
    bool tiled, packed, cube;
  };
  const Case layout_cases[] = {
      {6, 256, 256, 8, true, true, false},   // power of two, packed tail
      {6, 100, 60, 6, true, true, false},    // not a power of two, packed tail
      {6, 16, 16, 4, true, true, false},     // the base in the packed tail
      {6, 32, 128, 7, true, true, false},    // taller than wide
      {6, 128, 32, 7, true, false, false},   // wider than tall, no packing
      {2, 200, 72, 7, true, true, false},    // k_8
      {32, 64, 64, 6, true, true, false},    // 16_16_16_16_FLOAT
      {6, 96, 40, 3, false, false, false},   // linear mips
      {2, 64, 64, 6, false, true, false},    // linear, packed tail
      {6, 64, 64, 6, true, true, true},      // cube with mips
      {18, 128, 64, 7, true, true, false},   // DXT1 with mips
      {20, 64, 64, 6, true, true, false},    // DXT4_5 with mips
  };
  uint32_t failures = 0;
  constexpr uint32_t kBasePage = 0x10, kMipPage = 0x60;
  for (const Case& c : layout_cases) {
    const GuestFormat* host = FindGuestFormat(c.format);
    const LoadShader* shader = ShaderOf(c.format, false);
    const uint32_t block = host->block, bpb = host->bytes_per_block;
    const uint32_t pitch_div_32 = (((c.width + block - 1) / block * block) + 31) / 32;
    // The fetch constant: dword 0 pitch and tiling, 1 format and base, 2 size,
    // 3 swizzle xyzw, 4 mip range, 5 dimension, packing and mip address.
    std::array<uint32_t, 6> d{};
    d[0] = 2u | (pitch_div_32 << 22) | (uint32_t(c.tiled) << 31);
    d[1] = c.format | (kBasePage << 12);
    d[2] = (c.width - 1) | ((c.height - 1) << 13) | (c.cube ? 5u << 26 : 0u);
    d[3] = (0u | (1u << 3) | (2u << 6) | (3u << 9)) << 1;
    d[4] = c.mip_max << 6;
    d[5] = ((c.cube ? 3u : 1u) << 9) | (uint32_t(c.packed) << 11) | (kMipPage << 12);
    ++cases;
    std::string reason;
    GuestTextureLayout layout;
    if (!GetTextureLayout(d, layout, reason)) {
      out << "{\"kind\":\"layout-case\",\"format\":" << c.format << ",\"passed\":false,\"error\":\"" << reason << "\"}\n";
      ++failures;
      continue;
    }
    // The SDK's procedure: the stored levels (base, mips up to the packed tail) and slices.
    xenos::xe_gpu_texture_fetch_t fetch{};
    fetch.dword_0 = d[0], fetch.dword_1 = d[1], fetch.dword_2 = d[2];
    fetch.dword_3 = d[3], fetch.dword_4 = d[4], fetch.dword_5 = d[5];
    uint32_t w1, h1, d1, base_page, mip_page, mip_min, mip_max;
    texture_util::GetSubresourcesFromFetchConstant(fetch, &w1, &h1, &d1, &base_page, &mip_page, &mip_min, &mip_max);
    const uint32_t depth = d1 + 1, faces = c.cube ? depth : 1;
    const texture_util::TextureGuestLayout sdk = texture_util::GetGuestTextureLayout(
        fetch.dimension, fetch.pitch, c.width, c.height, depth, fetch.tiled, fetch.format, fetch.packed_mips,
        base_page != 0, mip_max);
    uint32_t level_failures = 0, compared = 0;
    for (uint32_t level = 0; level <= mip_max; ++level)
      for (uint32_t face = 0; face < faces; ++face) {
        const bool packed = level >= sdk.packed_level;
        const uint32_t stored = level == 0 ? 0 : (packed ? sdk.packed_level : level);
        const texture_util::TextureGuestLayout::Level& sl = level == 0 ? sdk.base : sdk.mips[stored];
        uint32_t guest_offset = level == 0 ? base_page << 12 : (mip_page << 12) + sdk.mip_offsets_bytes[stored];
        guest_offset += face * sl.array_slice_stride_bytes;
        // The stored level's extent: the whole tail when packed.
        const uint32_t level_w = std::max(c.width >> level, 1u), level_h = std::max(c.height >> level, 1u);
        const uint32_t size_x = packed ? sl.x_extent_blocks : (level_w + block - 1) / block;
        const uint32_t size_y = packed ? sl.y_extent_blocks : (level_h + block - 1) / block;
        const uint32_t x_per_thread = 1u << shader->x_blocks_per_thread_log2;
        const uint32_t host_pitch = Align(Align(size_x, x_per_thread) * shader->bytes_per_host_block, 256);
        const uint32_t constants[10] = {uint32_t(c.tiled) | (1u << 4) | (1u << 7), guest_offset,
                                        c.tiled ? sl.row_pitch_bytes / bpb : sl.row_pitch_bytes,
                                        sl.z_slice_stride_block_rows, size_x, size_y, 1, 0, host_pitch,
                                        packed ? sl.y_extent_blocks * block : level_h};
        const uint32_t groups_x = (size_x + (1u << (2 + shader->x_blocks_per_thread_log2)) - 1) >>
                                  (2 + shader->x_blocks_per_thread_log2);
        std::vector<uint8_t> sdk_data;
        if (!gpu.Load(*shader, constants, groups_x, (size_y + 31) >> 5, Align(host_pitch * size_y, 512), sdk_data)) {
          ++level_failures;
          continue;
        }
        uint32_t x0 = 0, y0 = 0;
        if (packed) {
          uint32_t z0;
          texture_util::GetPackedMipOffset(c.width, c.height, depth, fetch.format, level, x0, y0, z0);
        }
        // Ours.
        const LevelPlace& place = layout.levels[level];
        GuestLevel guest_level;
        guest_level.memory = guest.data() + place.address + face * place.face_stride;
        guest_level.region_bytes = place.region_bytes;
        guest_level.pitch_blocks = place.row_pitch_bytes / bpb;
        guest_level.row_pitch_bytes = place.row_pitch_bytes;
        guest_level.tiled = c.tiled;
        guest_level.x0_blocks = place.x0_blocks;
        guest_level.y0_blocks = place.y0_blocks;
        guest_level.width_blocks = (level_w + block - 1) / block;
        guest_level.height_blocks = (level_h + block - 1) / block;
        guest_level.bytes_per_block = bpb;
        std::vector<uint8_t> own;
        const bool read = place.address + face * place.face_stride + place.region_bytes <= guest.size() &&
                          ReadGuestLevel(guest_level, own);
        ++compared;
        bool equal = read && place.x0_blocks == x0 && place.y0_blocks == y0;
        const uint32_t row_bytes = guest_level.width_blocks * bpb;
        for (uint32_t y = 0; equal && y < guest_level.height_blocks; ++y)
          equal = !std::memcmp(&own[size_t(y) * row_bytes], &sdk_data[size_t(y0 + y) * host_pitch + size_t(x0) * bpb],
                               row_bytes);
        if (!equal) {
          ++level_failures;
          out << "{\"kind\":\"layout-level\",\"format\":" << c.format << ",\"size\":[" << c.width << "," << c.height
              << "],\"level\":" << level << ",\"face\":" << face << ",\"read\":" << (read ? "true" : "false")
              << ",\"offset\":[" << place.x0_blocks << "," << place.y0_blocks << "],\"sdk_offset\":[" << x0 << "," << y0
              << "],\"address\":" << place.address << ",\"sdk_address\":" << guest_offset << ",\"passed\":false}\n";
        }
      }
    const bool passed = !level_failures && compared == (mip_max + 1) * faces && layout.mip_max == mip_max;
    failures += !passed;
    out << "{\"kind\":\"layout-case\",\"format\":" << c.format << ",\"size\":[" << c.width << "," << c.height
        << "],\"tiled\":" << (c.tiled ? "true" : "false") << ",\"packed\":" << (c.packed ? "true" : "false")
        << ",\"cube\":" << (c.cube ? "true" : "false") << ",\"levels\":" << mip_max + 1 << ",\"packed_level\":"
        << int(sdk.packed_level) << ",\"compared\":" << compared << ",\"failures\":" << level_failures
        << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
  }
  return failures;
}

int main(int argc, char** argv) {
  if (argc != 2 || std::filesystem::exists(argv[1])) return 2;
  std::ofstream out(argv[1], std::ios::binary);
  Gpu gpu;
  if (!gpu.Init()) {
    out << "{\"kind\":\"verdict\",\"passed\":false,\"error\":\"" << gpu.error << "\",\"guest_executed\":false}\n";
    return 1;
  }
  // Random guest memory: the whole source buffer, the level at kGuestOffset.
  std::mt19937 random(0x9131u);
  std::vector<uint8_t> guest(kSourceBytes);
  for (auto& b : guest) b = uint8_t(random());
  void* mapped = nullptr;
  D3D12_RANGE none{0, 0};
  gpu.source->Map(0, &none, &mapped);
  std::memcpy(mapped, guest.data(), guest.size());
  gpu.source->Unmap(0, nullptr);

  uint32_t cases = 0, failures = 0, formats = 0;
  // The table holds the formats the game uses, and only them.
  {
    const uint32_t kept[] = {2, 4, 6, 18, 20, 22, 23, 32, 36, 37, 38, 49, 59, 60}, kept_decompressed[] = {18, 20, 49, 59};
    uint64_t want = 0, want_decompressed = 0, have = 0, have_decompressed = 0;
    for (uint32_t format : kept) want |= 1ull << format;
    for (uint32_t format : kept_decompressed) want_decompressed |= 1ull << format;
    for (uint32_t format = 0; format < 64; ++format) {
      have |= uint64_t(FindGuestFormat(format) != nullptr) << format;
      have_decompressed |= uint64_t(FindGuestFormat(format, true) != nullptr) << format;
    }
    const bool passed = have == want && have_decompressed == want_decompressed;
    ++cases;
    failures += !passed;
    out << "{\"kind\":\"table\",\"formats\":" << std::popcount(have) << ",\"decompressed\":"
        << std::popcount(have_decompressed) << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
  }
  for (uint32_t pass = 0; pass < 128; ++pass) {
    const uint32_t format = pass & 63;
    const bool decompressed = pass >= 64;  // The second pass: the decompressed host formats.
    const GuestFormat* host = FindGuestFormat(format, decompressed);
    if (!host) continue;
    ++formats;
    const bool texels = DecompressesBlocks(host->conversion);
    const LoadShader* shader = ShaderOf(format, decompressed);
    if (!shader) {
      out << "{\"kind\":\"case\",\"format\":" << format << ",\"passed\":false,\"error\":\"no-sdk-load-shader\"}\n";
      ++failures;
      continue;
    }
    const uint32_t bpb = host->bytes_per_block, block = host->block, host_bpb = host->host_bytes_per_block;
    if (host_bpb != shader->bytes_per_host_block) {
      out << "{\"kind\":\"case\",\"format\":" << format << ",\"passed\":false,\"error\":\"host-block-size\"}\n";
      ++failures;
      continue;
    }
    // An aligned size and an odd one (blocks of 4 texels keep whole blocks).
    // (A decompressing load writes texels: its odd size crops inside the last blocks.)
    const uint32_t sizes[2][2] = {{64, 32}, {block == 4 && !texels ? 44u : 37u, block == 4 && !texels ? 20u : 19u}};
    for (const auto& size : sizes)
      for (uint32_t tiled = 0; tiled < 2; ++tiled)
        for (uint32_t endian = 0; endian < 4; ++endian) {
          const uint32_t width_blocks = (size[0] + block - 1) / block, height_blocks = (size[1] + block - 1) / block;
          const uint32_t pitch_blocks = Align(width_blocks, 32), row_pitch = Align(width_blocks * bpb, 256);
          // The SDK's constants (LoadTextureDataFromResidentMemoryImpl, unscaled: scale 1 in both 3-bit fields);
          // a decompressing shader writes texels, block_width of them per guest block along X.
          const uint32_t x_per_thread = (1u << shader->x_blocks_per_thread_log2) * (texels ? block : 1);
          const uint32_t host_pitch =
              Align(Align(texels ? size[0] : width_blocks, x_per_thread) * shader->bytes_per_host_block, 256);
          const uint32_t constants[10] = {tiled | (endian << 2) | (1u << 4) | (1u << 7), kGuestOffset,
                                          tiled ? pitch_blocks : row_pitch, Align(height_blocks, 32), width_blocks,
                                          height_blocks, 1, 0, host_pitch, size[1]};
          const uint32_t groups_x = (width_blocks + (1u << (2 + shader->x_blocks_per_thread_log2)) - 1) >>
                                    (2 + shader->x_blocks_per_thread_log2);
          const uint32_t groups_y = (height_blocks + 31) >> 5;
          const uint32_t host_rows = texels ? height_blocks * block : height_blocks;
          const uint32_t bytes = Align(host_pitch * host_rows, 512);
          std::vector<uint8_t> sdk;
          ++cases;
          if (!gpu.Load(*shader, constants, groups_x, groups_y, bytes, sdk)) {
            out << "{\"kind\":\"case\",\"format\":" << format << ",\"passed\":false,\"error\":\"" << gpu.error
                << "\"}\n";
            ++failures;
            continue;
          }
          // The product's read of the same level.
          GuestLevel level;
          level.memory = guest.data() + kGuestOffset;
          level.region_bytes = kSourceBytes - kGuestOffset;
          level.pitch_blocks = pitch_blocks;
          level.row_pitch_bytes = row_pitch;
          level.tiled = tiled;
          level.endian = endian;
          level.width_blocks = width_blocks;
          level.height_blocks = height_blocks;
          level.bytes_per_block = bpb;
          std::vector<uint8_t> own;
          const bool read = ReadGuestLevel(level, own);
          ConvertLevel(host->conversion, own, width_blocks, height_blocks, size[0], size[1]);
          int64_t first_diff = -1;
          uint32_t diff_bytes = 0;
          // Decompressed: the texel rows cropped to the texture's size.
          const uint32_t row_bytes = (texels ? size[0] : width_blocks) * host_bpb;
          const uint32_t rows = texels ? size[1] : height_blocks;
          for (uint32_t y = 0; read && y < rows; ++y)
            for (uint32_t i = 0; i < row_bytes; ++i)
              if (own[size_t(y) * row_bytes + i] != sdk[size_t(y) * host_pitch + i]) {
                if (first_diff < 0) first_diff = int64_t(y) * row_bytes + i;
                ++diff_bytes;
              }
          const bool passed = read && first_diff < 0;
          failures += !passed;
          out << "{\"kind\":\"case\",\"format\":" << format << ",\"decompressed\":" << (decompressed ? "true" : "false")
              << ",\"shader\":\"" << shader->name
              << "\",\"tiled\":" << (tiled ? "true" : "false") << ",\"endian\":" << endian << ",\"size\":[" << size[0]
              << "," << size[1] << "],\"read\":" << (read ? "true" : "false") << ",\"diff_bytes\":" << diff_bytes
              << ",\"first_diff\":" << first_diff << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
        }
  }
  const uint32_t layout_failures = LayoutCases(gpu, guest, out, cases);
  failures += layout_failures;
  const bool passed = failures == 0 && cases > 0;
  out << "{\"kind\":\"verdict\",\"passed\":" << (passed ? "true" : "false") << ",\"formats\":" << formats
      << ",\"cases\":" << cases << ",\"failures\":" << failures
      << ",\"layout_failures\":" << layout_failures
      << ",\"guest_executed\":false}\n";
  return passed ? 0 : 1;
}
