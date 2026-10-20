#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "key_bindings.h"

// The game executable next to the launcher carries the native renderer next to
// the Xenos plugin (BROKENBOND_NATIVE_RENDERER_BOOTSTRAP builds).
#ifdef BROKENBOND_LAUNCHER_NATIVE_RENDERER
inline constexpr bool kNativeRendererBuilt = true;
#else
inline constexpr bool kNativeRendererBuilt = false;
#endif
#if defined(BROKENBOND_LAUNCHER_NATIVE_VULKAN)
inline constexpr bool kNativeVulkanBuilt = true;
#else
inline constexpr bool kNativeVulkanBuilt = false;
#endif
#if defined(BROKENBOND_LAUNCHER_NATIVE_D3D12_DISABLED)
inline constexpr bool kNativeD3d12Built = false;
#else
inline constexpr bool kNativeD3d12Built = true;
#endif

struct GameConfig {
  std::string gpu_plugin = "xenos";
  // "xenos" (the default) or "native" (native_backend); saved
  // only when the executable carries the native renderer.
  std::string renderer = "xenos";
  std::string native_backend = kNativeD3d12Built ? "d3d12" : "vulkan";
  // The graphics card by its name, for both renderers; empty = automatic.
  std::string graphics_device = "";
  bool skip_intro_videos = true;

  // Game language: 1 = English, 4 = French, 3 = German, 5 = Spanish, 6 = Italian.
  std::uint32_t user_language = 1;

  std::string input_backend = "sdl";
  // Keyboard and mouse mode, next to the controller: the mouse moves the
  // camera (the right stick) at mnk_sensitivity (1.0 = 100%), and each
  // controller input has its keys.
  bool mnk_mode = false;
  bool mnk_mouse = true;
  double mnk_sensitivity = 1.0;
  KeyBinds key_binds = DefaultKeyBinds();

  bool fullscreen = true;
  bool vsync = true;
  bool d3d12_host_vsync = true;
  double video_mode_refresh_rate = 60.0;
  // The SDK's (and Xenia's) default: the game's own 1280 x 720.
  int resolution_scale = 1;
  int draw_resolution_scale_x = 1;
  int draw_resolution_scale_y = 1;
  bool d3d12_allow_variable_refresh_rate_and_tearing = true;

  int anisotropic_override = 5;

  // Native renderer only, saved with the renderer choice.
  int msaa_samples = 0;  // 0 = the game's 4x, or 2, 4, 8
  bool transparency_antialiasing = false;
  // 30 = the game's; above it, the frames between the game's logic steps are
  // interpolated.
  int frame_rate = 30;
  double brightness = 0.5;  // 0.5 = the game's
  double internal_resolution_scale = 0.0;  // fractional scale; 0 = resolution_scale
  int max_frame_latency = 1;
  bool triple_buffering = false;

  std::string swap_post_effect = "fxaa";
  std::string present_effect = "cas";
  bool present_dither = false;
  bool show_fps_overlay = false;

  // 0.0 = 16:9 native (default), 2.3889 = 21:9, 3.5556 = 32:9
  double ultrawide_target_aspect = 0.0;

  std::string custom_game_root = "";
  // Saves and caches; empty keeps the project's existing user directory. Read only: the
  // launcher passes it to the game on the command line (main.cpp).
  std::string user_data_root = "";

  // Launcher interface: en (default), pt_BR, fr, de, es, it, ru.
  std::string launcher_language = "en";
};

class ConfigManager {
 public:
  static bool Load(const std::filesystem::path& toml_path, GameConfig& config);
  static bool Save(const std::filesystem::path& toml_path, const GameConfig& config);
};
