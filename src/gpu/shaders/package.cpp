// The embedded shader packages (package.h).
#include "gpu/shaders/package.h"
#include <cstring>
#include <mutex>
#include <windows.h>
#include <fmt/format.h>
#include "gpu/common/sha256.h"
// The inflate of stb_image (its zlib decoder), for the zlib containers.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "pc/stb_image.h"
#if BROKENBOND_SHADER_CACHE_SPIRV
#include "smolv.h"
#endif

namespace gpu {
// The containers embedded in the build (shaders/shader_cache_data.cpp); the
// SPIR-V one is empty in a build without the Vulkan backend.
extern const char kShaderCache[], kShaderCacheSpirv[];
extern const size_t kShaderCacheSize, kShaderCacheSpirvSize;

namespace {
// Record kinds of the payload, and the payload version of each format.
enum RecordKind : uint32_t {
  kDxilRecord = 0,
  kVariantRecord = 2,
  kSpirvRecord = 3,
  kManifestRecord = 4,
  kPassRecord = 5,
};
constexpr uint32_t kDxilPayloadVersion = 2, kSpirvPayloadVersion = 5;

uint32_t U32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
uint64_t U64(const uint8_t* p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}

// MSZIP through the Windows Compression API (the containers of before). Wine
// and Proton lack it (no export, or a stub that copies the input), so the
// Steam Deck drew nothing: every draw lost its shaders.
bool DecompressMszip(const uint8_t* packed, uint64_t packed_bytes, uint64_t payload_bytes, ShaderPackage& package,
                     std::string& error) {
  HMODULE cabinet = LoadLibraryW(L"cabinet.dll");
  using Create = BOOL(WINAPI*)(DWORD, void*, HANDLE*);
  using Run = BOOL(WINAPI*)(HANDLE, const void*, SIZE_T, void*, SIZE_T, SIZE_T*);
  using Close = BOOL(WINAPI*)(HANDLE);
  auto create = cabinet ? reinterpret_cast<Create>(GetProcAddress(cabinet, "CreateDecompressor")) : nullptr;
  auto run = cabinet ? reinterpret_cast<Run>(GetProcAddress(cabinet, "Decompress")) : nullptr;
  auto close = cabinet ? reinterpret_cast<Close>(GetProcAddress(cabinet, "CloseDecompressor")) : nullptr;
  HANDLE decompressor = nullptr;
  if (!create || !run || !close || !create(2 /* COMPRESS_ALGORITHM_MSZIP */, nullptr, &decompressor)) {
    error = "embedded-decompressor";
    return false;
  }
  package.payload.resize(size_t(payload_bytes));
  SIZE_T written = 0;
  const bool unpacked = run(decompressor, packed, SIZE_T(packed_bytes), package.payload.data(),
                            SIZE_T(payload_bytes), &written);
  close(decompressor);
  if (!unpacked || written != payload_bytes) {
    error = "embedded-decompress";
    return false;
  }
  return true;
}

// The payload of a container: "NRSC", u32 version 1, u32 algorithm 3 (zlib)
// or 2 (MSZIP), u32 0, u64 payload bytes, u64 compressed bytes, the SHA-256 of
// the payload, then the compressed payload.
bool Decompress(const uint8_t* c, size_t size, ShaderPackage& package, std::string& error) {
  constexpr size_t kHeader = 64;  // Magic, 3 x u32, 2 x u64, SHA-256.
  const uint32_t algorithm = size >= kHeader ? U32(c + 8) : 0;
  if (size < kHeader || std::memcmp(c, "NRSC", 4) || U32(c + 4) != 1 || (algorithm != 2 && algorithm != 3)) {
    error = "embedded-container";
    return false;
  }
  const uint64_t payload_bytes = U64(c + 16), packed_bytes = U64(c + 24);
  if (packed_bytes != size - kHeader || payload_bytes > (uint64_t(1) << 30) || packed_bytes > (uint64_t(1) << 30)) {
    error = "embedded-sizes";
    return false;
  }
  if (algorithm == 2) {
    if (!DecompressMszip(c + kHeader, packed_bytes, payload_bytes, package, error)) return false;
  } else {
    package.payload.resize(size_t(payload_bytes));
    const int written = stbi_zlib_decode_buffer(reinterpret_cast<char*>(package.payload.data()), int(payload_bytes),
                                                reinterpret_cast<const char*>(c + kHeader), int(packed_bytes));
    if (written < 0 || uint64_t(written) != payload_bytes) {
      error = "embedded-inflate";
      return false;
    }
  }
  std::string expected;
  for (unsigned i = 0; i < 32; ++i) expected += fmt::format("{:02x}", c[32 + i]);
  package.payload_sha256 = Sha256Hex(package.payload.data(), package.payload.size());
  if (package.payload_sha256 != expected) {
    error = "embedded-payload-sha256";
    return false;
  }
  return true;
}
}  // namespace

// The payload: "NRSP", u32 version, u32 index bytes, u32 files, the index text
// padded to 4, one record per file (u32 kind, u32 name bytes, u32 offset into
// the blob area, u32 size, the name padded to 4), then the blob area.
bool UnpackShaderPackage(ShaderFormat format, ShaderPackage& package, std::string& error) {
  const bool spirv = format == ShaderFormat::kSpirv;
  const auto* c = reinterpret_cast<const uint8_t*>(spirv ? kShaderCacheSpirv : kShaderCache);
  const size_t size = spirv ? kShaderCacheSpirvSize : kShaderCacheSize;
  if (!size) {
    error = "embedded-format-absent";
    return false;
  }
  package.format = format;
  package.container_bytes = size;
  if (!Decompress(c, size, package, error)) return false;
  const uint8_t* p = package.payload.data();
  const size_t n = package.payload.size();
  if (n < 16 || std::memcmp(p, "NRSP", 4) || U32(p + 4) != (spirv ? kSpirvPayloadVersion : kDxilPayloadVersion)) {
    error = "embedded-payload";
    return false;
  }
  const uint32_t index_bytes = U32(p + 8), files = U32(p + 12);
  size_t at = 16;
  if (index_bytes > n - at) {
    error = "embedded-index";
    return false;
  }
  package.index.assign(reinterpret_cast<const char*>(p + at), index_bytes);
  at += (index_bytes + 3) & ~size_t(3);
  struct Record {
    uint32_t kind, offset, size;
    std::string name;
  };
  auto admitted = [spirv](uint32_t kind) {
    return spirv ? kind == kSpirvRecord || kind == kManifestRecord || kind == kPassRecord
                 : kind == kDxilRecord || kind == kVariantRecord;
  };
  std::vector<Record> records;
  for (uint32_t i = 0; i < files; ++i) {
    if (at + 16 > n) {
      error = "embedded-files";
      return false;
    }
    Record r{U32(p + at), U32(p + at + 8), U32(p + at + 12), {}};
    const uint32_t name = U32(p + at + 4);
    at += 16;
    if (name > 64 || at + name > n || !admitted(r.kind)) {
      error = "embedded-files";
      return false;
    }
    r.name.assign(reinterpret_cast<const char*>(p + at), name);
    at += (name + 3) & ~size_t(3);
    records.push_back(std::move(r));
  }
  for (const auto& r : records) {
    if (r.offset > n - at || r.size > n - at - r.offset) {
      error = "embedded-blob";
      return false;
    }
    const std::span<const uint8_t> blob(p + at + r.offset, r.size);
    if (r.kind == kManifestRecord) package.manifest.assign(reinterpret_cast<const char*>(blob.data()), blob.size());
    else if (r.kind == kPassRecord) package.passes[r.name] = blob;
    else (r.kind == kVariantRecord ? package.variants : package.modules)[r.name] = blob;
  }
  return true;
}

const ShaderPackage* EmbeddedShaderPackage(ShaderFormat format, std::string& error) {
  struct Unpacked {
    std::once_flag once;
    ShaderPackage package;
    bool ok = false;
    std::string error;
  };
  static Unpacked formats[2];
  Unpacked& unpacked = formats[format == ShaderFormat::kSpirv];
  std::call_once(unpacked.once, [&] { unpacked.ok = UnpackShaderPackage(format, unpacked.package, unpacked.error); });
  if (!unpacked.ok) {
    error = unpacked.error;
    return nullptr;
  }
  return &unpacked.package;
}

std::vector<uint8_t> DecodeShaderModule(const ShaderPackage& package, std::span<const uint8_t> stored) {
  if (package.format == ShaderFormat::kDxil) return {stored.begin(), stored.end()};
#if BROKENBOND_SHADER_CACHE_SPIRV
  std::vector<uint8_t> module(smolv::GetDecodedBufferSize(stored.data(), stored.size()));
  if (module.empty() || !smolv::Decode(stored.data(), stored.size(), module.data(), module.size())) return {};
  return module;
#else
  return {};
#endif
}
}  // namespace gpu
