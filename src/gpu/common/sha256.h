#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace gpu {
// SHA-256 by the Windows CNG provider: identities of files and copies for the
// diagnostics; never a guest value or a draw input.
class Sha256 final {
 public:
  Sha256() {
    ok_ = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
          BCRYPT_SUCCESS(BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0));
  }
  ~Sha256() {
    if (hash_) BCryptDestroyHash(hash_);
    if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
  }
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;
  void Update(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    while (ok_ && size) {
      const ULONG n = ULONG((std::min)(size, size_t(1) << 30));
      ok_ = BCRYPT_SUCCESS(BCryptHashData(hash_, const_cast<PUCHAR>(p), n, 0));
      p += n;
      size -= n;
    }
  }
  // Lowercase hex digest; empty when CNG failed. The object is spent afterwards.
  std::string Finish() {
    std::array<uint8_t, 32> digest{};
    if (!ok_ || !BCRYPT_SUCCESS(BCryptFinishHash(hash_, digest.data(), ULONG(digest.size()), 0))) return "";
    ok_ = false;
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (uint8_t b : digest) {
      out += hex[b >> 4];
      out += hex[b & 15];
    }
    return out;
  }
 private:
  BCRYPT_ALG_HANDLE algorithm_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
  bool ok_ = false;
};

inline std::string Sha256Hex(const void* data, size_t size) {
  Sha256 hash;
  hash.Update(data, size);
  return hash.Finish();
}

// Whole file by path, shared with writers; empty on any failure.
inline std::string Sha256File(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return "";
  Sha256 hash;
  std::array<uint8_t, 65536> chunk{};
  bool ok = true;
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(file, chunk.data(), DWORD(chunk.size()), &read, nullptr)) { ok = false; break; }
    if (!read) break;
    hash.Update(chunk.data(), read);
  }
  CloseHandle(file);
  return ok ? hash.Finish() : "";
}
}  // namespace gpu
