#pragma once

#include <filesystem>

// ImGui theme reused from the Rise of a Ninja launcher.
namespace launcher_theme {

void Apply();
void LoadFonts(const std::filesystem::path& base_dir);

}  // namespace launcher_theme
