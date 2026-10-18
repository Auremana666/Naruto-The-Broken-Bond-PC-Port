#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct GameConfig {
  std::string gpu_plugin = "xenos";

  // Game language: 1 = English, 4 = French, 3 = German, 5 = Spanish, 6 = Italian.
  std::uint32_t user_language = 1;

  std::string input_backend = "sdl";
  bool mnk_mode = false;

  bool fullscreen = true;
  bool vsync = true;
  int resolution_scale = 1;
  int draw_resolution_scale_x = 1;
  int draw_resolution_scale_y = 1;

  int anisotropic_override = 3;

  std::string swap_post_effect = "none";
  std::string present_effect = "bilinear";
  bool present_dither = false;

  std::string custom_game_root = "";

};

class ConfigManager {
 public:
  static bool Load(const std::filesystem::path& toml_path, GameConfig& config);
  static bool Save(const std::filesystem::path& toml_path, const GameConfig& config);
};
