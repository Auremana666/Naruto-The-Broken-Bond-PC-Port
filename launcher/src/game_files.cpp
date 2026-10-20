#include "game_files.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <algorithm>
#include <fstream>

bool ValidateGameExecutable(const std::filesystem::path& path, std::string& error) {
  error.clear();
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "Could not read default.xex in the selected game folder.";
    return false;
  }
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    error = "Could not read the game executable size.";
    return false;
  }
  return ValidateGameExecutable(file, size, error);
}

bool ValidateGameExecutable(std::istream& file, uint64_t size, std::string& error) {
  struct HashContext {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    ~HashContext() {
      if (hash) BCryptDestroyHash(hash);
      if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
  } context;
  error = "Could not verify the game executable.";
  if (BCryptOpenAlgorithmProvider(&context.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
      BCryptCreateHash(context.algorithm, &context.hash, nullptr, 0, nullptr, 0, 0) < 0)
    return false;
  std::array<unsigned char, 65536> buffer;
  while (size) {
    const auto count = static_cast<std::streamsize>(std::min<uint64_t>(size, buffer.size()));
    file.read(reinterpret_cast<char*>(buffer.data()), count);
    if (file.gcount() != count) return false;
    if (BCryptHashData(context.hash, buffer.data(), static_cast<ULONG>(file.gcount()), 0) < 0)
      return false;
    size -= count;
  }
  std::array<unsigned char, 32> digest;
  if (BCryptFinishHash(context.hash, digest.data(), digest.size(), 0) < 0) return false;
  constexpr char hex[] = "0123456789abcdef";
  std::string actual;
  for (auto byte : digest) {
    actual += hex[byte >> 4];
    actual += hex[byte & 15];
  }
  if (actual != "8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576") {
    error = "Unsupported default.xex. Select the supported Naruto: The Broken Bond revision.";
    return false;
  }
  error.clear();
  return true;
}
