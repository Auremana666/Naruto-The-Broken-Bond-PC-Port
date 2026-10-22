// Draws the PC overlay's latest frame (gpu/overlay.h) over the back buffer at
// the end of the present pass, on the render owner thread.
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "gpu/overlay.h"

namespace gpu::d3d12 {

class OverlayRenderer {
 public:
  // Upload memory of the open batch, valid until its fence.
  struct Upload {
    ID3D12Resource* buffer = nullptr;
    uint64_t offset = 0;
    uint8_t* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
  };
  using UploadFn = std::function<bool(size_t bytes, Upload& upload)>;
  // Keeps a resource alive until the open batch completes.
  using RetainFn = std::function<void(Microsoft::WRL::ComPtr<ID3D12Resource>)>;

  // Records the overlay into commands (the back buffer bound as rtv, in the
  // render target state). False only on a device failure; an empty overlay
  // records nothing.
  bool Draw(ID3D12Device* device, ID3D12GraphicsCommandList* commands, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
            uint32_t width, uint32_t height, const UploadFn& upload, const RetainFn& retain);
  void Release();

 private:
  struct GpuTexture {
    std::weak_ptr<const overlay::Texture> source;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    uint32_t slot = 0;
  };
  static constexpr uint32_t kSlots = 256;
  bool Initialize(ID3D12Device* device);
  bool Prepare(ID3D12Device* device, ID3D12GraphicsCommandList* commands, const overlay::Texture& texture,
               GpuTexture& gpu, const UploadFn& upload);

  bool initialized_ = false, failed_ = false;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  uint32_t descriptor_step_ = 0;
  std::unordered_map<const overlay::Texture*, GpuTexture> textures_;
  std::vector<uint32_t> free_slots_;
  // Slots of released textures, reusable once frames that could read them completed.
  std::vector<std::pair<uint32_t, uint64_t>> released_slots_;
  uint64_t frames_ = 0;
};

}  // namespace gpu::d3d12
