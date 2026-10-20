#include "config_manager.h"

#include <windows.h>
#include <cmath>
#include <fstream>
#include <toml++/toml.hpp>

bool ConfigManager::Load(const std::filesystem::path& toml_path, GameConfig& config) {
  try {
    std::ifstream input(toml_path);
    if (!input) return false;
    const auto tbl = toml::parse(input);
    if (auto v = tbl["gpu_plugin"].value<std::string>()) config.gpu_plugin = *v;
    if (auto v = tbl["graphics_device"].value<std::string>()) config.graphics_device = *v;
    if (auto v = tbl["input_backend"].value<std::string>()) config.input_backend = *v;
    if (auto v = tbl["swap_post_effect"].value<std::string>()) config.swap_post_effect = *v;
    if (auto v = tbl["present_effect"].value<std::string>()) config.present_effect = *v;
    if (auto v = tbl["custom_game_root"].value<std::string>()) config.custom_game_root = *v;
    if (auto v = tbl["launcher_language"].value<std::string>()) config.launcher_language = *v;
    if (auto v = tbl["user_language"].value<std::uint32_t>()) config.user_language = *v;
    if (auto v = tbl["skip_intro_videos"].value<bool>()) config.skip_intro_videos = *v;
    if (auto v = tbl["mnk_mode"].value<bool>()) config.mnk_mode = *v;
    if (auto v = tbl["mnk_mouse"].value<bool>()) config.mnk_mouse = *v;
    if (auto v = tbl["fullscreen"].value<bool>()) config.fullscreen = *v;
    if (auto v = tbl["vsync"].value<bool>()) config.vsync = *v;
    if (auto v = tbl["d3d12_host_vsync"].value<bool>()) config.d3d12_host_vsync = *v;
    if (auto v = tbl["d3d12_allow_variable_refresh_rate_and_tearing"].value<bool>()) config.d3d12_allow_variable_refresh_rate_and_tearing = *v;
    if (auto v = tbl["present_dither"].value<bool>()) config.present_dither = *v;
    if (auto v = tbl["show_fps_overlay"].value<bool>()) config.show_fps_overlay = *v;
    if (auto v = tbl["mnk_sensitivity"].value<double>()) if (std::isfinite(*v)) config.mnk_sensitivity = *v;
    if (auto v = tbl["video_mode_refresh_rate"].value<double>()) if (std::isfinite(*v)) config.video_mode_refresh_rate = *v;
    if (auto v = tbl["ultrawide_target_aspect"].value<double>()) if (std::isfinite(*v)) config.ultrawide_target_aspect = *v;
    if (auto v = tbl["anisotropic_override"].value<int>()) config.anisotropic_override = *v;
    if (auto v = tbl["msaa_samples"].value<int>()) config.msaa_samples = *v;
    if (auto v = tbl["frame_rate"].value<int>()) config.frame_rate = *v;
    if (auto v = tbl["max_frame_latency"].value<int>()) config.max_frame_latency = *v;
    if (auto v = tbl["transparency_antialiasing"].value<bool>()) config.transparency_antialiasing = *v;
    if (auto v = tbl["triple_buffering"].value<bool>()) config.triple_buffering = *v;
    if (auto v = tbl["brightness"].value<double>()) if (std::isfinite(*v)) config.brightness = *v;
    if (auto v = tbl["internal_resolution_scale"].value<double>()) if (std::isfinite(*v)) config.internal_resolution_scale = *v;
    if (auto v = tbl["renderer"].value<std::string>()) config.renderer = *v == "native" ? "native" : "xenos";
    if (auto v = tbl["native_backend"].value<std::string>()) config.native_backend = *v;
    if (auto v = tbl["user_data_root"].value<std::string>()) config.user_data_root = *v;
    if (auto v = tbl["resolution_scale"].value<int>()) {
      config.resolution_scale = config.draw_resolution_scale_x = config.draw_resolution_scale_y = *v;
    }
    for (std::size_t i = 0; i < kKeyActionCount; ++i) {
      if (auto v = tbl[KeyActionOption(i)].value<std::string>()) config.key_binds[i] = *v;
    }
    return true;
  } catch (const toml::parse_error&) {
    return false;
  }
}

bool ConfigManager::Save(const std::filesystem::path& toml_path, const GameConfig& config) {
  toml::table tbl;
  std::error_code ec;
  const bool exists = std::filesystem::exists(toml_path, ec);
  if (ec) return false;
  if (exists) {
    try {
      std::ifstream input(toml_path);
      if (!input) return false;
      tbl = toml::parse(input);
    } catch (...) {
      // Preserve unreadable or malformed user configuration for manual repair.
      return false;
    }
  }
  tbl.insert_or_assign("gpu_plugin", config.gpu_plugin);
  tbl.insert_or_assign("graphics_device", config.graphics_device);
  tbl.insert_or_assign("input_backend", config.input_backend);
  tbl.insert_or_assign("swap_post_effect", config.swap_post_effect);
  tbl.insert_or_assign("present_effect", config.present_effect);
  tbl.insert_or_assign("launcher_language", config.launcher_language);
  tbl.insert_or_assign("user_language", config.user_language);
  tbl.insert_or_assign("skip_intro_videos", config.skip_intro_videos);
  tbl.insert_or_assign("mnk_mode", config.mnk_mode);
  tbl.insert_or_assign("mnk_mouse", config.mnk_mouse);
  tbl.insert_or_assign("fullscreen", config.fullscreen);
  tbl.insert_or_assign("vsync", config.vsync);
  tbl.insert_or_assign("d3d12_host_vsync", config.d3d12_host_vsync);
  tbl.insert_or_assign("d3d12_allow_variable_refresh_rate_and_tearing", config.d3d12_allow_variable_refresh_rate_and_tearing);
  tbl.insert_or_assign("present_dither", config.present_dither);
  tbl.insert_or_assign("show_fps_overlay", config.show_fps_overlay);
  tbl.insert_or_assign("mnk_sensitivity", config.mnk_sensitivity);
  tbl.insert_or_assign("video_mode_refresh_rate", config.video_mode_refresh_rate);
  tbl.insert_or_assign("anisotropic_override", config.anisotropic_override);
  if (kNativeRendererBuilt) {
    tbl.insert_or_assign("renderer", config.renderer == "native" ? "native" : "xenos");
    tbl.insert_or_assign("native_backend", config.native_backend);
    tbl.insert_or_assign("msaa_samples", config.msaa_samples);
    tbl.insert_or_assign("frame_rate", config.frame_rate);
    tbl.insert_or_assign("max_frame_latency", config.max_frame_latency);
    tbl.insert_or_assign("transparency_antialiasing", config.transparency_antialiasing);
    tbl.insert_or_assign("triple_buffering", config.triple_buffering);
    tbl.insert_or_assign("brightness", config.brightness);
    tbl.insert_or_assign("internal_resolution_scale", config.internal_resolution_scale);
  }
  tbl.insert_or_assign("resolution_scale", config.resolution_scale);
  tbl.insert_or_assign("draw_resolution_scale_x", config.resolution_scale);
  tbl.insert_or_assign("draw_resolution_scale_y", config.resolution_scale);
  for (std::size_t i = 0; i < kKeyActionCount; ++i) {
    tbl.insert_or_assign(KeyActionOption(i), config.key_binds[i]);
  }
  if (!config.custom_game_root.empty()) {
    tbl.insert_or_assign("custom_game_root", config.custom_game_root);
  } else {
    tbl.erase("custom_game_root");
  }
  // user_data_root is read only, so an absent override stays absent.
  // Replace only after a complete write; failed saves keep the old settings.
  auto temporary = toml_path;
  temporary += L".launcher-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
  std::ofstream output(temporary, std::ios::trunc | std::ios::binary);
  if (!output) return false;
  output << "# broken_bond - runtime configuration generated by Naruto: The Broken Bond Launcher\n\n";
  output << tbl << "\n";
  output.flush();
  const bool written = output.good();
  output.close();
  if (!written || output.fail() || !MoveFileExW(temporary.c_str(), toml_path.c_str(),
                                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temporary, ec);
    return false;
  }
  return true;
}
