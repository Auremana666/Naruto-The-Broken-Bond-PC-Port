#include "graphics_devices.h"

#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#endif

const std::vector<std::string>& GraphicsDevices() {
  static const std::vector<std::string> devices = [] {
    std::vector<std::string> names;
#ifdef _WIN32
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return names;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) == S_OK; ++index) {
      DXGI_ADAPTER_DESC1 desc;
      char name[256];
      // As the game: no software or remote adapter, Direct3D 12 at feature level 11_0.
      if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
          !(desc.Flags & (DXGI_ADAPTER_FLAG_SOFTWARE | DXGI_ADAPTER_FLAG_REMOTE)) &&
          SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
          WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr) > 1 &&
          std::find(names.begin(), names.end(), name) == names.end()) {
        names.emplace_back(name);
      }
      adapter->Release();
    }
    factory->Release();
#endif
    return names;
  }();
  return devices;
}
