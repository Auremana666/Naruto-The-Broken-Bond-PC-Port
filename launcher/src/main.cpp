#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>

#include <windows.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <dxgi.h>

#include "config_manager.h"
#include "game_files.h"
#include "game_paths.h"
#include "localization.h"
#include "resource.h"
#include "texture_loader.h"
#include "ui.h"
#include "theme.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static void CreateRenderTarget() {
  ID3D11Texture2D* pBackBuffer = nullptr;
  if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
  }
}

static void CleanupRenderTarget() {
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = nullptr;
  }
}

static bool CreateDeviceD3D(HWND hWnd) {
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 0;
  sd.BufferDesc.Height = 0;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hWnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT createDeviceFlags = 0;
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[2] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_0,
  };

  HRESULT res = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
      featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
      &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  if (res == DXGI_ERROR_UNSUPPORTED) {
    res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  }
  if (FAILED(res)) {
    return false;
  }

  CreateRenderTarget();
  return true;
}

static void CleanupDeviceD3D() {
  CleanupRenderTarget();
  if (g_pSwapChain) {
    g_pSwapChain->Release();
    g_pSwapChain = nullptr;
  }
  if (g_pd3dDeviceContext) {
    g_pd3dDeviceContext->Release();
    g_pd3dDeviceContext = nullptr;
  }
  if (g_pd3dDevice) {
    g_pd3dDevice->Release();
    g_pd3dDevice = nullptr;
  }
}

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  // Resolve base directory and configuration path
  const char* base_path_str = SDL_GetBasePath();
  std::filesystem::path base_dir = base_path_str ? game_paths::FromUtf8(base_path_str)
                                               : std::filesystem::current_path();
  base_dir = std::filesystem::canonical(base_dir);
  std::filesystem::path toml_path = base_dir / "broken_bond.toml";

  GameConfig config;
  ConfigManager::Load(toml_path, config);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
    std::cerr << "Failed to initialize SDL3: " << SDL_GetError() << std::endl;
    return 1;
  }

  const int window_w = 1060;
  const int window_h = 720;
  SDL_Window* window = SDL_CreateWindow(
      Tr(TextId::WindowTitle),
      window_w, window_h,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

  if (!window) {
    std::cerr << "Failed to create SDL_Window: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return 1;
  }

  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_SetWindowMinimumSize(window, 1000, 680);

  HWND hwnd = (HWND)SDL_GetPointerProperty(
      SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  if (!hwnd || !CreateDeviceD3D(hwnd)) {
    std::cerr << "Failed to initialize Direct3D 11 device." << std::endl;
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Shared resource handles remain valid for the lifetime of the process.
  HICON large_icon = static_cast<HICON>(LoadImageW(
      GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_LAUNCHER), IMAGE_ICON,
      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
  HICON small_icon = static_cast<HICON>(LoadImageW(
      GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_LAUNCHER), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
  if (large_icon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
  if (small_icon) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));

  // Load cover artwork texture
  LoadedTexture cover_tex = LoadTextureFromFile(
      g_pd3dDevice, base_dir / "assets" / "banner-broken-bond.png");
  if (!cover_tex.srv) {
    cover_tex = LoadTextureFromFile(g_pd3dDevice, base_dir / "assets" / "cover.jpg");
  }
  if (!cover_tex.srv) {
    cover_tex = LoadTextureFromFile(g_pd3dDevice, base_dir / "cover.jpg");
  }

  // Setup ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr; // Do not save layout .ini to disk

  LauncherUI::SetupTheme();
  launcher_theme::LoadFonts(base_dir);

  ImGui_ImplSDL3_InitForD3D(window);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  bool running = true;
  bool request_launch = false;
  std::string status_msg = "";

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window)) {
        running = false;
      }
      if (event.type == SDL_EVENT_WINDOW_RESIZED &&
          event.window.windowID == SDL_GetWindowID(window)) {
        CleanupRenderTarget();
        g_pSwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        CreateRenderTarget();
      }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    LauncherUI::Render(window, base_dir, config, request_launch, status_msg, cover_tex.srv,
                       cover_tex.width, cover_tex.height);

    ImGui::Render();
    const float clear_color[4] = { 0.09f, 0.09f, 0.11f, 1.00f };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0); // VSync enabled

    if (request_launch) {
      const std::filesystem::path game_data = game_paths::Find(base_dir, config.custom_game_root);
      if (game_data.empty()) {
        status_msg = Tr(TextId::FilesMissingDesc);
        request_launch = false;
        continue;
      }
      if (!ValidateGameExecutable(game_data / "default.xex", status_msg)) {
        request_launch = false;
        continue;
      }
      if (!ConfigManager::Save(toml_path, config)) {
        status_msg = Tr(TextId::SaveFailed);
        request_launch = false;
        continue;
      }

      std::filesystem::path game_exe = base_dir / "broken_bond.exe";
      if (std::filesystem::exists(game_exe)) {
        std::wstring cmdline = L"\"" + game_exe.wstring() + L"\"";
        const auto user_dir = game_paths::UserDataRoot(base_dir);
        std::error_code path_error;
        std::filesystem::create_directories(user_dir, path_error);
        if (path_error) {
          status_msg = "Could not create the save folder: " + path_error.message();
          request_launch = false;
          continue;
        }
        cmdline += L" --user_data_root=\"" + user_dir.wstring() + L"\" --gpu_plugin=xenos";
        cmdline += L" --game_data_root=\"" + game_data.wstring() + L"\"";

        std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
        cmd_buf.push_back(L'\0');

        STARTUPINFOW si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        std::wstring cwd = base_dir.wstring();
        BOOL success = CreateProcessW(
            nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi);

        if (success) {
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          status_msg = Tr(TextId::LaunchSuccessMsg);
          // Exit launcher once the game process is spawned
          running = false;
        } else {
          status_msg = std::string(Tr(TextId::LaunchFailedMsg)) + " Win32 Error " + std::to_string(GetLastError());
        }
      } else {
        status_msg = std::string(Tr(TextId::ExeNotFoundMsg)) + game_exe.string();
      }
      request_launch = false;
    }
  }

  // Save settings before closing
  if (!ConfigManager::Save(toml_path, config)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Settings not saved",
                             Tr(TextId::SaveFailed), window);
  }

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  cover_tex.Release();
  CleanupDeviceD3D();
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
