#include "config_manager.h"
#include "game_files.h"
#include "game_paths.h"

#include <filesystem>
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
    const auto root = fs::absolute(game_paths::FromUtf8(argv[1]));
    const auto path = root / "settings" / "broken_bond.toml";
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
    Require(!ConfigManager::Save(root / "absent" / "settings.toml", config),
            "An unwritable destination reported success");

    const auto repo = root / "repository";
    const auto base = repo / "build" / "game";
    fs::create_directories(base);
    Write(repo / "broken_bond_manifest.toml", "# Test marker\n");
    Write(repo / "game" / "default.xex", "Test fixture, not a game executable");
    Require(game_paths::Find(base, "") == fs::canonical(repo / "game"),
            "Repository game folder not found");
    Require(game_paths::UserDataRoot(base) == repo / "build" / "user",
            "Launcher and run script would use different saves");
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
