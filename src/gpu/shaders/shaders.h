#pragma once

// Shaders of the D3D boundary (shaders.cpp): the XenosRecomp cache embedded in
// the build with the spec constant variants linked there, the bound shader
// objects and their identification against the cache, and the registry of the
// objects by their birth.
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>
#include <rex/ppc/context.h>

namespace gpu {
struct CachedShader {
  std::string name;
  uint32_t virtual_size = 0, spec_mask = 0;  // spec_mask: spec constants the body uses (index v3)
  // The index keeps no microcode (identification only by hash). The first
  // object identified gives the
  // entry its microcode, the same words by the hash, and what derives from it
  // (code_known, under the cache mutex).
  bool code_known = false;
  bool textures = false;
  const char* tfetch_override = nullptr;  // First tfetch filter field that overrides the fetch constant.
  // (fetch constant, fetch dimension) of each texture fetch, distinct.
  std::vector<std::pair<uint32_t, uint32_t>> fetch_constants;
  std::vector<uint32_t> code;
  std::shared_ptr<const std::vector<uint8_t>> dxil;
  // The variants linked in the build, by spec constants, taken from the package on first use.
  std::unordered_map<uint32_t, std::shared_ptr<const std::vector<uint8_t>>> linked;
};
struct ShaderCache {
  std::once_flag once;
  bool loaded = false;
  std::string index, dxil_dir, error;
  std::mutex mutex;
  std::unordered_map<uint64_t, CachedShader> by_hash[2];  // 0 PS, 1 VS; key: virtual part then microcode
  size_t entries = 0;
  // The package embedded in the build, decompressed once: the DXIL of each
  // shader name and its spec constant variants, by "<name>+spec<value>".
  std::vector<uint8_t> payload;
  std::unordered_map<std::string, std::span<const uint8_t>> dxil, variants;
  std::atomic<uint64_t> variants_loaded{0};  // Variants taken from the package.
  std::string record;  // JSON fields of the load, for the first draw record.
  // The shaders by cache name (the names of the pipeline keys), for
  // the precompiled pipelines; filled once by the load, then read only.
  std::unordered_map<std::string, CachedShader*> by_name;
};
extern ShaderCache shader_cache;

// One bound shader: object, container and the microcode currently in memory.
struct BoundShader {
  uint32_t object = 0, container = 0, virtual_size = 0, code_physical = 0, code_size = 0;
  std::vector<uint32_t> code;
  uint64_t virtual_hash = 0;
  uint64_t hash = 0;  // FNV-1a of the virtual part followed by the microcode, the cache key.
};

// Samplers of a bound shader, from the constant table of its
// container (D3DXSHADER_CONSTANTTABLE after a size word, at the offset in
// +0x10). XenosRecomp names the descriptor indices by the register index (2D
// at shared +4r, 3D at +64+4r, cube at +128+4r, sampler at +192+4r), and its
// texture fetches use the same index as fetch constant. A sampler array gives
// one entry per register of its RegisterCount.
struct ShaderSampler {
  uint32_t reg = 0, type = 0;  // D3DXPARAMETER_TYPE: 10 sampler, 11 1D, 12 2D, 13 3D, 14 cube
  std::string name;
};

// Shader objects are translated once, at their birth. The Jade engine
// creates every D3D shader object in 82179200 (VS: container at +872, code at
// +32) and 82179380 (PS: container at +40, code at +24) from the SDB entry;
// the hooks at the end of this file register the object with its unpatched
// microcode. The draw looks the bound objects up by address; the identification
// against the cache is exact (no fetch-word tolerance) and happens once. An
// entry is forgotten by the D750 and replaced by a new birth at the same
// address (generation); a bind whose object no longer holds the code pointer
// of the birth is stale and takes the per-draw path, counted.
struct ShaderEntry {
  BoundShader bound;  // Unpatched microcode, as copied from the SDB.
  bool vertex = false;
  uint32_t code_base = 0;  // +32 (VS) or +24 (PS) at birth: the stale check.
  uint64_t generation = 0;
  uint32_t lr = 0;
  bool identified = false, exact = false;
  CachedShader* cached = nullptr;
  uint32_t candidates = 0, differing = 0;
  bool samplers_read = false, samplers_ok = false;
  std::vector<ShaderSampler> samplers;
};
// The record of a bound shader object (container, metadata, microcode).
std::string ShaderRecord(uint8_t* base, uint32_t object, bool vertex);
void LoadShaderCache();
std::shared_ptr<const std::vector<uint8_t>> LoadDxil(CachedShader& shader);
// DXIL of a cached shader for the spec constants of the draw. A shader
// whose body uses spec constants has a mask in the index; a value inside it
// takes the variant the build linked (build-sdb-shader-variants.py, the
// library of the shader with another exporting g_SpecConstants() with that
// value), and value 0 the DXIL compiled with the stub. A value without a
// variant fails ("variant-missing"): the draw is skipped and counted.
std::shared_ptr<const std::vector<uint8_t>> LoadShader(CachedShader& shader, uint32_t spec, std::string& error);
// Transparency antialiasing (option transparency_antialiasing). An
// alpha-tested pixel shader drawn into a
// multisampled target with an opaque color blend (ONE, ZERO: the cut-out
// vegetation, which the title already draws with alpha to mask) takes the
// fork's alpha to coverage variant instead of the clip (the sharpened alpha,
// boosted by the mip level), and its pipeline enables alpha to coverage. A
// draw whose color blend weighs the alpha (translucent and additive sprites)
// keeps the clip: the sharpened alpha makes the glow sprites solid quads.
// Given the spec constants of the
// draw, the shader's spec mask, the samples of the color target and
// RB_BLENDCONTROL, returns the spec constants to link. The draw and the
// precompiled list both go through it.
constexpr uint32_t kSpecAlphaTest = 1u << 1, kSpecAlphaToCoverage = 1u << 3;
uint32_t TransparencySpec(uint32_t spec, uint32_t spec_mask, uint32_t samples, uint32_t blend_control);
bool ReadBoundShader(uint8_t* base, uint32_t object, bool vertex, BoundShader& s, const char** why = nullptr);
// By the hash of the virtual part and the microcode (exact); candidates
// is 1 when the key is in the cache, differing always 0.
CachedShader* IdentifyShader(const BoundShader& s, bool vertex, uint32_t& candidates, uint32_t& differing);
bool ReadSamplers(uint8_t* base, const BoundShader& s, std::vector<ShaderSampler>& out);
// The register of the Float4 constant called name in the container's constant
// table; -1 when absent or the table is unreadable.
int32_t FindFloatConstant(uint8_t* base, const BoundShader& s, std::string_view name);
// The entry of a bound object, or null (missed or stale), counted.
std::shared_ptr<ShaderEntry> FindShaderEntry(uint8_t* base, uint32_t object, bool vertex);
// Exact identification of an entry, once (render thread).
void IdentifyEntry(ShaderEntry& e);
bool ForgetShaderObject(uint32_t object);
std::string ShaderRegistrySummary();
extern std::atomic<uint64_t> shader_identified_tolerant;
}  // namespace gpu
