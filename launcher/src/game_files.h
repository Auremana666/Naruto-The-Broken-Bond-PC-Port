#pragma once

#include <filesystem>
#include <string>

// Verify the revision supported by the generated code before starting the game.
bool ValidateGameExecutable(const std::filesystem::path& path, std::string& error);
