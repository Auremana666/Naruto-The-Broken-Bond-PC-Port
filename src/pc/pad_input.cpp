#include "pad_input.h"

#include <initializer_list>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <dxgi1_2.h>

namespace {

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetStateFn LoadXInput() {
  for (const wchar_t* name : {L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll"}) {
    if (HMODULE module = LoadLibraryW(name)) {
      if (auto fn = reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"))) {
        return fn;
      }
    }
  }
  return nullptr;
}

}  // namespace

PadRaw ReadPadRaw() {
  static const XInputGetStateFn get_state = LoadXInput();
  PadRaw raw;
  if (!get_state) {
    return raw;
  }
  for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
    XINPUT_STATE state{};
    if (get_state(user, &state) != ERROR_SUCCESS) {
      continue;
    }
    raw.buttons |= state.Gamepad.wButtons;
    if (state.Gamepad.bLeftTrigger > raw.left_trigger) raw.left_trigger = state.Gamepad.bLeftTrigger;
    if (state.Gamepad.bRightTrigger > raw.right_trigger) raw.right_trigger = state.Gamepad.bRightTrigger;
  }
  return raw;
}

PadRaw ReadPadRawAt(int index, bool* connected) {
  static const XInputGetStateFn get_state = LoadXInput();
  PadRaw raw;
  XINPUT_STATE state{};
  const bool ok = get_state && index >= 0 && index < XUSER_MAX_COUNT &&
                  get_state(static_cast<DWORD>(index), &state) == ERROR_SUCCESS;
  if (connected) *connected = ok;
  if (ok) {
    raw.buttons = state.Gamepad.wButtons;
    raw.left_trigger = state.Gamepad.bLeftTrigger;
    raw.right_trigger = state.Gamepad.bRightTrigger;
  }
  return raw;
}

uint32_t ReadPads() {
  static const XInputGetStateFn get_state = LoadXInput();
  if (!get_state) {
    return 0;
  }
  uint32_t bits = 0;
  for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
    XINPUT_STATE state{};
    if (get_state(user, &state) != ERROR_SUCCESS) {
      continue;
    }
    const WORD b = state.Gamepad.wButtons;
    const SHORT lx = state.Gamepad.sThumbLX;
    const SHORT ly = state.Gamepad.sThumbLY;
    if ((b & XINPUT_GAMEPAD_DPAD_UP) || ly > 16000) bits |= kPadUp;
    if ((b & XINPUT_GAMEPAD_DPAD_DOWN) || ly < -16000) bits |= kPadDown;
    if ((b & XINPUT_GAMEPAD_DPAD_LEFT) || lx < -16000) bits |= kPadLeft;
    if ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || lx > 16000) bits |= kPadRight;
    if (b & XINPUT_GAMEPAD_A) bits |= kPadA;
    if (b & XINPUT_GAMEPAD_B) bits |= kPadB;
    if (b & XINPUT_GAMEPAD_X) bits |= kPadX;
    if (b & XINPUT_GAMEPAD_Y) bits |= kPadY;
    if (b & XINPUT_GAMEPAD_START) bits |= kPadStart;
    if (b & XINPUT_GAMEPAD_BACK) bits |= kPadBack;
  }
  return bits;
}

bool CtrlHeld() {
  return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

void ShowGameError(const char* text) {
  MessageBoxA(nullptr, text, "Naruto The Broken Bond PC", MB_OK | MB_ICONERROR);
}

bool ReadResource(int id, const void** data, size_t* size) {
  HMODULE module = GetModuleHandleW(nullptr);
  HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));  // RT_RCDATA
  HGLOBAL handle = info ? LoadResource(module, info) : nullptr;
  const void* bytes = handle ? LockResource(handle) : nullptr;
  if (!bytes) return false;
  *data = bytes;
  *size = SizeofResource(module, info);
  return true;
}

std::vector<std::pair<int, std::string>> ListGraphicsCards() {
  std::vector<std::pair<int, std::string>> cards;
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
    return cards;
  }
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      char name[256] = {};
      WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
      cards.emplace_back(static_cast<int>(i), name);
    }
    adapter->Release();
  }
  factory->Release();
  return cards;
}
