#pragma once

#include <filesystem>
#include <string>

// The optional broken_bond.version file next to the executable takes precedence
// over the build's BROKENBOND_VERSION definition. Development builds show "dev".
std::string AppVersion(const std::filesystem::path& base_dir);
