#include "title_screen.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_drawer.h>

#include "options_menu.h"
#include "pad_input.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

REXCVAR_DEFINE_BOOL(show_title_screen, true, "Game",
                    "Show the PC title screen when the game starts");

namespace {

std::atomic<bool> g_open = false;

constexpr int kNarutoResource = 101;
constexpr int kExtraPictureResource = 102;
constexpr int kExtraSoundResource = 103;
constexpr int kLogoResource = 104;
// The secret word is only stored as its FNV-1a hash (and its length).
constexpr uint64_t kSecretHash = 0x48bb34d8edbb078aull;
constexpr size_t kSecretLength = 6;

uint64_t Fnv1a(const char* text, size_t length) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < length; ++i) {
    hash = (hash ^ static_cast<unsigned char>(text[i])) * 0x100000001b3ull;
  }
  return hash;
}
constexpr ImU32 kRed = IM_COL32(230, 30, 40, 255);
constexpr double kShowSeconds = 30.0;  // then it continues by itself
constexpr double kFadeIn = 0.35;
constexpr double kFadeOut = 0.45;

// Colors of the PC Options menu: the game's blue and orange, and its dark outline.
constexpr ImU32 kBlue = IM_COL32(32, 146, 255, 255);
constexpr ImU32 kOrange = IM_COL32(255, 140, 24, 255);
constexpr ImU32 kOutline = IM_COL32(6, 12, 38, 255);
constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);

ImU32 WithAlpha(ImU32 color, float alpha) {
  const auto a = static_cast<int>(((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f));
  return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

ImFont* Font() { return g_menu_font ? g_menu_font : ImGui::GetFont(); }

float TextWidth(float size, const std::string& text) {
  return Font()->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

void Outlined(ImDrawList* dl, ImVec2 pos, float size, ImU32 color, const std::string& text,
              float alpha, float outline) {
  static const float kDirs[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1},
                                    {-0.7f, -0.7f}, {0.7f, -0.7f}, {-0.7f, 0.7f}, {0.7f, 0.7f}};
  for (const auto& d : kDirs) {
    dl->AddText(Font(), size, ImVec2(pos.x + d[0] * outline, pos.y + d[1] * outline),
                WithAlpha(kOutline, alpha), text.c_str());
  }
  dl->AddText(Font(), size, pos, WithAlpha(color, alpha), text.c_str());
}

// The key that opens PC Options, as the player has it bound.
std::string OptionsKey() {
  std::string key = rex::cvar::GetFlagByName("bind_options");
  const auto comma = key.find(',');
  if (comma != std::string::npos) key = key.substr(0, comma);
  std::transform(key.begin(), key.end(), key.begin(), ::toupper);
  return key.empty() ? "F1" : key;
}

}  // namespace

TitleScreen::TitleScreen(rex::ui::ImGuiDrawer* drawer, std::filesystem::path folder,
                         std::function<void()> on_closed)
    : ImGuiDialog(drawer), on_closed_(std::move(on_closed)) {
  g_open.store(true);
  // Remove sound files a previous run could not (the game was closed while it played).
  wchar_t temp[MAX_PATH];
  if (GetTempPathW(MAX_PATH, temp)) {
    WIN32_FIND_DATAW found;
    const std::wstring pattern = std::wstring(temp) + L"nbb*.tmp";
    HANDLE find = FindFirstFileW(pattern.c_str(), &found);
    if (find != INVALID_HANDLE_VALUE) {
      do {
        DeleteFileW((std::wstring(temp) + found.cFileName).c_str());
      } while (FindNextFileW(find, &found));
      FindClose(find);
    }
  }
  naruto_ = LoadResource(kNarutoResource);
  extra_ = LoadResource(kExtraPictureResource);
  logo_ = Load(folder / "logo.png");  // a replacement next to the executable, if any
  if (!logo_.texture) logo_ = LoadResource(kLogoResource);
  pad_held_ = ReadPads();
}

bool TitleScreen::IsOpen() {
  return g_open.load();
}

TitleScreen::Picture TitleScreen::LoadResource(int id) {
  HMODULE module = GetModuleHandleW(nullptr);
  HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));  // RT_RCDATA
  if (!info) return {};
  HGLOBAL handle = ::LoadResource(module, info);
  const auto* data = handle ? static_cast<const stbi_uc*>(LockResource(handle)) : nullptr;
  if (!data) return {};
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(SizeofResource(module, info)), &w,
                                          &h, &channels, 4);
  return Upload(pixels, w, h);
}

TitleScreen::~TitleScreen() {
  StopSound();
}

void TitleScreen::TriggerSecret() {
  secret_time_ = time_;
  // MCI plays MP3 from a file only, so the sound is written to a temporary file while it plays.
  HMODULE module = GetModuleHandleW(nullptr);
  HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(kExtraSoundResource), MAKEINTRESOURCEW(10));
  HGLOBAL handle = info ? ::LoadResource(module, info) : nullptr;
  const void* data = handle ? LockResource(handle) : nullptr;
  if (!data) return;
  wchar_t folder[MAX_PATH];
  wchar_t path[MAX_PATH];
  if (!GetTempPathW(MAX_PATH, folder) || !GetTempFileNameW(folder, L"nbb", 0, path)) return;
  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, data, SizeofResource(module, info), &written, nullptr);
  CloseHandle(file);
  sound_file_ = path;
  const std::wstring open = L"open \"" + sound_file_ + L"\" type mpegvideo alias nbb_extra";
  if (mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0) {
    mciSendStringW(L"play nbb_extra", nullptr, 0, nullptr);
  }
}

void TitleScreen::StopSound() {
  if (sound_file_.empty()) return;
  mciSendStringW(L"close nbb_extra", nullptr, 0, nullptr);
  DeleteFileW(sound_file_.c_str());
  sound_file_.clear();
}

TitleScreen::Picture TitleScreen::Load(const std::filesystem::path& path) {
  Picture picture;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return picture;
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
  if (!pixels) REXLOG_WARN("[title] Could not read {}", path.string());
  return Upload(pixels, w, h);
}

TitleScreen::Picture TitleScreen::Upload(unsigned char* pixels, int w, int h) {
  Picture picture;
  auto* immediate = imgui_drawer() ? imgui_drawer()->immediate_drawer() : nullptr;
  if (!pixels) return picture;
  // Nearly invisible pixels (AI-generated art leaves a faint haze) become fully transparent.
  for (int i = 0; i < w * h; ++i) {
    if (pixels[i * 4 + 3] < 12) pixels[i * 4 + 3] = 0;
  }
  if (immediate) {
    picture.texture = immediate->CreateTexture(static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                               rex::ui::ImmediateTextureFilter::kLinear, false,
                                               pixels);
  }
  stbi_image_free(pixels);
  if (picture.texture) {
    picture.width = w;
    picture.height = h;
  }
  return picture;
}

void TitleScreen::Dismiss() {
  if (close_time_ < 0.0) close_time_ = time_;
}

void TitleScreen::OnClose() {
  g_open.store(false);
  if (on_closed_) on_closed_();
}

void TitleScreen::OnDraw(ImGuiIO& io) {
  time_ += io.DeltaTime;

  // Continue: Enter, Start or A on a controller, a click, or the time running out.
  const uint32_t pads = ReadPads();
  const uint32_t pressed = pads & ~pad_held_;
  pad_held_ = pads;
  // Letters typed, for the easter egg.
  // (Key presses, not text: the window does not deliver typed text to ImGui.)
  for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k) {
    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) {
      typed_.push_back(static_cast<char>('a' + (k - ImGuiKey_A)));
    }
  }
  for (int k = 0; k <= 9; ++k) {
    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + k), false) ||
        ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + k), false)) {
      typed_.push_back(static_cast<char>('0' + k));
    }
  }
  if (typed_.size() > 16) typed_.erase(0, typed_.size() - 16);
  if (secret_time_ < 0.0 && close_time_ < 0.0 && extra_.texture && typed_.size() >= kSecretLength &&
      Fnv1a(typed_.data() + typed_.size() - kSecretLength, kSecretLength) == kSecretHash) {
    secret_name_ = typed_.substr(typed_.size() - kSecretLength);
    for (char& c : secret_name_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    TriggerSecret();
  }

  if (close_time_ < 0.0) {
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (time_ > 0.3 && (enter || (pressed & (kPadStart | kPadA)) ||
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
      Dismiss();
    }
    // 30 seconds, counted again from the easter egg so it can be enjoyed.
    if (time_ - std::max(0.0, secret_time_) >= kShowSeconds) Dismiss();
  }
  float alpha = std::min(1.0f, static_cast<float>(time_ / kFadeIn));
  if (close_time_ >= 0.0) {
    const double t = (time_ - close_time_) / kFadeOut;
    if (t >= 1.0) {
      StopSound();
      Close();
      return;
    }
    alpha *= static_cast<float>(1.0 - t);
  }

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float w = io.DisplaySize.x;
  const float h = io.DisplaySize.y;
  const float s = h / 1080.0f;

  // White, a little warmer towards the bottom.
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, h), WithAlpha(kWhite, alpha),
                              WithAlpha(kWhite, alpha), WithAlpha(IM_COL32(246, 242, 236, 255), alpha),
                              WithAlpha(IM_COL32(246, 242, 236, 255), alpha));

  // Naruto on the right, standing on the bottom edge; after the secret, the extra character.
  const float secret =
      secret_time_ < 0.0 ? 0.0f : std::min(1.0f, static_cast<float>((time_ - secret_time_) / 0.5));
  float column_right = w - 80.0f * s;
  auto character = [&](const Picture& picture, float fade, float bottom_gap) {
    if (!picture.texture || fade <= 0.0f) return;
    const float ph = std::min(h - 60.0f * s, 1000.0f * s) + bottom_gap;
    const float pw = ph * picture.width / picture.height;
    const float px = w - pw - 90.0f * s;
    const float py = h - ph - 20.0f * s + bottom_gap;
    const float bob = std::sin(static_cast<float>(time_) * 1.6f) * 6.0f * s;
    dl->AddImage(reinterpret_cast<ImTextureID>(picture.texture.get()), ImVec2(px, py + bob),
                 ImVec2(px + pw, py + ph + bob), ImVec2(0, 0), ImVec2(1, 1),
                 WithAlpha(kWhite, alpha * fade));
    column_right = std::min(column_right, px - 40.0f * s);
  };
  character(naruto_, 1.0f - secret, 0.0f);
  // The extra picture ends at the legs, so it runs off the bottom edge.
  character(extra_, secret, 40.0f * s);

  // Everything else is centered in the column left of Naruto.
  const float column_left = 100.0f * s;
  const float center = (column_left + column_right) * 0.5f;
  auto centered = [&](float y, float size, ImU32 color, const std::string& text, float a,
                      float outline) {
    Outlined(dl, ImVec2(center - TextWidth(size, text) * 0.5f, y), size, color, text, a,
             outline * s);
  };

  // "PC" above the logo, as a badge.
  {
    const float size = 64.0f * s;
    const float tw = TextWidth(size, "PC");
    const ImVec2 a(center - tw * 0.5f - 34.0f * s, 70.0f * s);
    const ImVec2 b(center + tw * 0.5f + 34.0f * s, 70.0f * s + size + 16.0f * s);
    dl->AddRectFilled(ImVec2(a.x - 4 * s, a.y - 4 * s), ImVec2(b.x + 4 * s, b.y + 4 * s),
                      WithAlpha(kOutline, alpha), 18.0f * s);
    dl->AddRectFilled(a, b, WithAlpha(kOrange, alpha), 14.0f * s);
    Outlined(dl, ImVec2(center - tw * 0.5f, a.y + 8.0f * s), size, kWhite, "PC", alpha, 3.0f * s);
  }

  // The logo: the picture if there is one, else the name in the menu font.
  float y = 180.0f * s;
  if (logo_.texture) {
    const float max_w = std::min(column_right - column_left, 900.0f * s);
    const float max_h = 380.0f * s;
    float lw = max_w;
    float lh = lw * logo_.height / logo_.width;
    if (lh > max_h) {
      lh = max_h;
      lw = lh * logo_.width / logo_.height;
    }
    dl->AddImage(reinterpret_cast<ImTextureID>(logo_.texture.get()),
                 ImVec2(center - lw * 0.5f, y), ImVec2(center + lw * 0.5f, y + lh), ImVec2(0, 0),
                 ImVec2(1, 1), WithAlpha(kWhite, alpha));
    y += lh + 50.0f * s;
  } else {
    centered(y, 190.0f * s, kOrange, "NARUTO", alpha, 6.0f);
    y += 200.0f * s;
    centered(y, 74.0f * s, kBlue, "THE BROKEN BOND", alpha, 4.0f);
    y += 140.0f * s;
  }

  // How to open PC Options: blue, with the keys in orange.
  {
    const float size = 44.0f * s;
    const std::pair<std::string, ImU32> parts[] = {{"PC OPTIONS: PRESS ", kBlue},
                                                    {OptionsKey(), kOrange},
                                                    {" OR ", kBlue},
                                                    {"BACK + START", kOrange}};
    float total = 0.0f;
    for (const auto& [text, color] : parts) total += TextWidth(size, text);
    float x = center - total * 0.5f;
    for (const auto& [text, color] : parts) {
      Outlined(dl, ImVec2(x, y), size, color, text, alpha, 3.0f * s);
      x += TextWidth(size, text);
    }
    y += size + 120.0f * s;
  }

  // Press Enter, flashing.
  {
    const float pulse = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(static_cast<float>(time_) * 5.0f));
    centered(y, 72.0f * s, kOrange, "PRESS ENTER", alpha * pulse, 4.0f);
  }

  // Credits along the bottom; the secret crosses out Auremana666 for the typed name in red.
  {
    const float size = 30.0f * s;
    const float cy = h - 70.0f * s;
    const std::string lead = "MADE BY CLAUDE AI AND ";
    const std::string old_name = "AUREMANA666";
    const std::string new_name = " " + secret_name_;
    const float total = TextWidth(size, lead) + TextWidth(size, old_name) +
                        TextWidth(size, new_name) * secret;
    float x = center - total * 0.5f;
    Outlined(dl, ImVec2(x, cy), size, kBlue, lead, alpha, 2.0f * s);
    x += TextWidth(size, lead);
    Outlined(dl, ImVec2(x, cy), size, kBlue, old_name, alpha * (1.0f - 0.45f * secret), 2.0f * s);
    if (secret > 0.0f) {
      const float line = (TextWidth(size, old_name) + 8.0f * s) * secret;
      const float ly = cy + size * 0.55f;
      dl->AddLine(ImVec2(x - 4.0f * s, ly), ImVec2(x - 4.0f * s + line, ly), WithAlpha(kRed, alpha),
                  5.0f * s);
      x += TextWidth(size, old_name);
      Outlined(dl, ImVec2(x, cy), size, kRed, new_name, alpha * secret, 2.0f * s);
    }
  }
}
