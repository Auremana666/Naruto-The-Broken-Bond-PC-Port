#include "config_manager.h"
#include "game_files.h"
#include "game_paths.h"
#include "process_arguments.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace fs = std::filesystem;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  out << text;
  Require(out.good(), "Could not write test fixture");
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  try {
    const auto root = fs::absolute(game_paths::FromUtf8(argv[1])) /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = root / "settings" / "broken_bond.toml";
    for (const auto* value : {L"D:\\Game Files\\", L"C:\\", L"C:\\Saves ç\\", L"", L"a\\\"b"}) {
      const auto command = L"launcher.exe " + QuoteProcessArgument(value) + L" --next";
      int count = 0;
      auto arguments = CommandLineToArgvW(command.c_str(), &count);
      const bool round_trip = arguments && count == 3 && std::wstring(arguments[1]) == value &&
                              std::wstring(arguments[2]) == L"--next";
      LocalFree(arguments);
      Require(round_trip, "Process argument quoting changed a path or swallowed the next option");
    }
    Write(path, "user_language = 4\nupdate_data_root = 'D:/Title Update'\n");
    GameConfig config;
    Require(ConfigManager::Load(path, config), "Configuration failed to load");
    Require(config.user_language == 4, "Game language was not loaded");
    config.mnk_mode = true;
    config.fullscreen = false;
    config.resolution_scale = 2;
    config.anisotropic_override = -1;
    config.custom_game_root = "D:/Game Files/Broken Bond";
    Require(ConfigManager::Save(path, config), "Configuration failed to save");
    GameConfig reloaded;
    Require(ConfigManager::Load(path, reloaded), "Configuration failed to reload");
    Require(reloaded.user_language == 4 && reloaded.mnk_mode && !reloaded.fullscreen &&
                reloaded.resolution_scale == 2 && reloaded.anisotropic_override == -1 &&
                reloaded.custom_game_root == config.custom_game_root,
            "Settings did not persist");
    auto table = toml::parse_file(path.string());
    Require(table["update_data_root"].value<std::string>() == "D:/Title Update",
            "Saving erased an unmanaged setting");
    Require(table["draw_resolution_scale_x"].value<int>() == 2 &&
                table["draw_resolution_scale_y"].value<int>() == 2,
            "Resolution axes do not match");
    config.custom_game_root.clear();
    Require(ConfigManager::Save(path, config), "Path reset failed");
    Require(!toml::parse_file(path.string()).contains("custom_game_root"),
            "Path reset did not remove the override");
    const std::string malformed = "user_language = [\n";
    Write(path, malformed);
    Require(!ConfigManager::Load(path, config), "Malformed TOML was accepted");
    Require(!ConfigManager::Save(path, config), "Malformed user config was overwritten");
    std::ifstream preserved(path);
    Require(std::string(std::istreambuf_iterator<char>(preserved), {}) == malformed,
            "Malformed config was not preserved");
    preserved.close();
    Require(!ConfigManager::Save(root / "absent" / "settings.toml", config),
            "An unwritable destination reported success");

    Write(path, "renderer = 'native'\nuser_data_root = 'my saves'\nkeybind_a = 'J,K,L'\n");
    GameConfig options;
    Require(options.renderer == "xenos" && options.frame_rate == 30, "Xenos defaults changed");
    Require(ConfigManager::Load(path, options), "Extended settings failed to load");
    Require(options.key_binds[0] == "J,K,L", "Loading truncated hand-written key alternatives");
    options.graphics_device = "Test graphics adapter";
    options.native_backend = "vulkan";
    options.launcher_language = "pt_BR";
    options.mnk_mouse = false;
    options.mnk_sensitivity = 1.75;
    options.skip_intro_videos = false;
    options.show_fps_overlay = true;
    options.key_binds[1] = ",Q";
    options.msaa_samples = 8;
    options.frame_rate = 120;
    options.brightness = 0.6;
    options.internal_resolution_scale = 1.5;
    options.max_frame_latency = 3;
    options.triple_buffering = true;
    options.transparency_antialiasing = true;
    Require(ConfigManager::Save(path, options), "Extended settings failed to save");
    GameConfig restored;
    Require(ConfigManager::Load(path, restored), "Extended settings failed to reload");
    Require(restored.graphics_device == options.graphics_device && restored.launcher_language == "pt_BR" &&
                !restored.mnk_mouse && restored.mnk_sensitivity == 1.75 && !restored.skip_intro_videos &&
                restored.show_fps_overlay && restored.key_binds[1] == ",Q" && restored.key_binds[0] == "J,K,L" &&
                restored.user_data_root == "my saves", "Extended settings did not persist");
    const auto extended = toml::parse_file(path.string());
    Require(!extended.contains("ultrawide_target_aspect"), "Saving introduced an unsupported aspect option");
    if (kNativeRendererBuilt) {
      Require(restored.msaa_samples == 8 && restored.frame_rate == 120 && restored.brightness == 0.6 &&
                  restored.native_backend == "vulkan" &&
                  restored.internal_resolution_scale == 1.5 && restored.max_frame_latency == 3 &&
                  restored.triple_buffering && restored.transparency_antialiasing,
              "Native renderer options did not persist");
    } else {
      Require(!extended.contains("frame_rate") && extended["renderer"].value<std::string>() == "native",
              "Xenos launcher overwrote unmanaged native settings");
    }
    Write(path, "renderer = 'xenos'\nnative_backend = 'vulkan'\n");
    GameConfig xenos;
    Require(ConfigManager::Load(path, xenos) && xenos.renderer == "xenos" &&
                xenos.native_backend == "vulkan", "Loading Vulkan preference changed the Xenos selection");
    Require(ConfigManager::Save(path, xenos), "Xenos selection failed to save");
    Require(toml::parse_file(path.string())["renderer"].value<std::string>() == "xenos",
            "Saving a native-capable configuration changed the Xenos selection");

    const auto repo = root / "repository";
    const auto base = repo / "build" / "game";
    fs::create_directories(base);
    Write(repo / "broken_bond_manifest.toml", "# Test marker\n");
    Write(repo / "game" / "default.xex", "Test fixture, not a game executable");
    Require(game_paths::Find(base, "") == fs::canonical(repo / "game"),
            "Repository game folder not found");
    Require(game_paths::UserDataRoot(base) == repo / "build" / "user",
            "Launcher and run script would use different saves");
    const auto native_base = repo / "build" / "native";
    Require(game_paths::Find(native_base, "") == fs::canonical(repo / "game") &&
                game_paths::UserDataRoot(native_base) == repo / "build" / "user",
            "Native launcher did not share repository game files and saves");
    Require(game_paths::UserDataRoot(base, "my saves") == base / "my saves",
            "Custom save folder is not relative to the launcher");
    Write(base / "game" / "default.xex", "Local fixture");
    Require(game_paths::Find(base, "") == fs::canonical(base / "game"),
            "Adjacent game folder did not take priority");
    Require(game_paths::Find(base, "missing").empty(),
            "Missing custom folder silently fell back to another game");
    Write(base / "custom folder" / "default.xex", "Custom fixture");
    Require(game_paths::Find(base, "custom folder") == fs::canonical(base / "custom folder"),
            "Custom relative paths are not relative to the launcher");
    const auto portable = root / "portable";
    Require(game_paths::UserDataRoot(portable) == portable / "user",
            "Portable save folder is not adjacent to the launcher");
    std::string error;
    Require(!ValidateGameExecutable(base / "game" / "default.xex", error) &&
                error.starts_with("Unsupported"), "Wrong game executable was accepted");
    Require(!ValidateGameExecutable(root / "missing.xex", error),
            "Missing executable was accepted");
    if (argc == 3)
      Require(ValidateGameExecutable(game_paths::FromUtf8(argv[2]), error),
              "The supported local game executable was rejected");
    std::cout << "Launcher configuration, game paths, and executable validation passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
