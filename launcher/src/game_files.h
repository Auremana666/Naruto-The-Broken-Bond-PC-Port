#pragma once

#include <filesystem>
#include <cstdint>
#include <istream>
#include <string>

// Verify the revision supported by the generated code before starting the game.
bool ValidateGameExecutable(const std::filesystem::path& path, std::string& error);

// Hash exactly one XEX from the current stream position, including inside an ISO.
bool ValidateGameExecutable(std::istream& file, uint64_t size, std::string& error);
