#pragma once

#include <filesystem>

// The Rise launcher's shared ImGui theme (dark slate and Naruto Chakra orange).
namespace launcher_theme {

void Apply();
void LoadFonts(const std::filesystem::path& base_dir);

}  // namespace launcher_theme
