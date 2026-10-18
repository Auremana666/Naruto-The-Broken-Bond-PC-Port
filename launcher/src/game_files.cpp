#include "game_files.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <fstream>

bool ValidateGameExecutable(const std::filesystem::path& path, std::string& error) {
  error.clear();
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "Could not read default.xex in the selected game folder.";
    return false;
  }
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
  while (file) {
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    if (BCryptHashData(context.hash, buffer.data(), static_cast<ULONG>(file.gcount()), 0) < 0)
      return false;
  }
  if (!file.eof()) return false;
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
