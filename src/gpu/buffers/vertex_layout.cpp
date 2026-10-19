// Vertex input of the D3D boundary (vertex_layout.h).
#include "gpu/buffers/vertex_layout.h"
#include "gpu/d3d/hooks.h"
#include "gpu/d3d/draw.h"
#include "gpu/common/guest_memory.h"
#include "gpu/common/hash.h"
#include "gpu/common/records.h"
#include "gpu/host/services.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dxgiformat.h>
#include <array>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
#include <dxcapi.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xmemory.h>

using namespace gpu;

namespace gpu {
const char* kSemantics[] = {"POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD", "TANGENT",
                            "BINORMAL", "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH", "SAMPLE"};

// Xenos vertex format (vfetch dword 1) to DXGI; 0 when not covered yet.
uint32_t VertexFormat(uint32_t format, bool integer, bool is_signed) {
  switch (format) {
    case 6: return integer ? (is_signed ? DXGI_FORMAT_R8G8B8A8_SINT : DXGI_FORMAT_R8G8B8A8_UINT)
                           : (is_signed ? DXGI_FORMAT_R8G8B8A8_SNORM : DXGI_FORMAT_R8G8B8A8_UNORM);
    // 16-bit pairs: only TEXCOORD, whose order XenosRecomp restores by g_SwappedTexcoords.
    case 25: return integer ? (is_signed ? DXGI_FORMAT_R16G16_SINT : DXGI_FORMAT_R16G16_UINT)
                            : (is_signed ? DXGI_FORMAT_R16G16_SNORM : DXGI_FORMAT_R16G16_UNORM);
    case 26: return integer ? (is_signed ? DXGI_FORMAT_R16G16B16A16_SINT : DXGI_FORMAT_R16G16B16A16_UINT)
                            : (is_signed ? DXGI_FORMAT_R16G16B16A16_SNORM : DXGI_FORMAT_R16G16B16A16_UNORM);
    case 31: return DXGI_FORMAT_R16G16_FLOAT;
    case 32: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case 36: return DXGI_FORMAT_R32_FLOAT;
    case 37: return DXGI_FORMAT_R32G32_FLOAT;
    case 38: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case 57: return DXGI_FORMAT_R32G32B32_FLOAT;
    default: return 0;
  }
}

// NORMAL, TANGENT and BINORMAL are uint4 inputs of XenosRecomp read
// through tfetchR11G11B10. With SPEC_CONSTANT_R11G11B10_NORMAL it unpacks the
// 10_11_11 word; otherwise it takes the bits as float (asfloat). The cache is
// built with g_SpecConstants() == 0, so float formats get the UINT format of
// the same size, and 10_11_11 (format 16) is expanded on the CPU by the same
// arithmetic into a separate stream of four float bit patterns.
bool UintUsage(uint32_t usage) { return usage == 3 || usage == 6 || usage == 7; }
uint32_t UintVertexFormat(uint32_t format) {
  switch (format) {
    case 16: case 31: case 32: return DXGI_FORMAT_R32G32B32A32_UINT;  // Expanded stream.
    case 36: return DXGI_FORMAT_R32_UINT;
    case 37: return DXGI_FORMAT_R32G32_UINT;
    case 38: return DXGI_FORMAT_R32G32B32A32_UINT;
    case 57: return DXGI_FORMAT_R32G32B32_UINT;
    default: return 0;
  }
}
// The vertex fetch is decoded in the shader. The shaders of the cache come
// from the repository's fork of XenosRecomp (tools/xenosrecomp, NARUTO_RECOMP):
// every vertex input is a
// uint4 of the raw words of its element, bound R32[G32[B32[A32]]]_UINT, and
// vfetchNaruto decodes it by its descriptor in the shared constants (c20..c23,
// one per element in fetch address order). The descriptor holds the format
// (bits 0..5), signed (6),
// integer (7), the DEC3N rule of 10_11_11 in NORMAL, TANGENT and BINORMAL (8)
// and the source of each output component (9..20: 0..3 a component in hardware
// order, X in the low bits of the swapped dword; 4 zero; 5 one). The typed
// formats stay for the template shaders of the START policy.
uint32_t FetchComponents(uint32_t format) {
  switch (format) {
    case 36: return 1;
    case 25: case 31: case 37: return 2;
    case 16: case 57: return 3;
    case 6: case 7: case 26: case 32: case 38: return 4;
    default: return 0;
  }
}
uint32_t FetchBytes(uint32_t format) {
  switch (format) {
    case 6: case 7: case 16: case 25: case 31: case 36: return 4;
    case 26: case 32: case 37: return 8;
    case 57: return 12;
    case 38: return 16;
    default: return 0;
  }
}
uint32_t RawVertexFormat(uint32_t bytes) {
  return bytes == 4 ? DXGI_FORMAT_R32_UINT : bytes == 8 ? DXGI_FORMAT_R32G32_UINT
         : bytes == 12 ? DXGI_FORMAT_R32G32B32_UINT : DXGI_FORMAT_R32G32B32A32_UINT;
}
constexpr uint32_t Selectors(uint32_t x, uint32_t y, uint32_t z, uint32_t w) { return x | y << 3 | z << 6 | w << 9; }
// The components of n, then the defaults of the input assembler: y and z 0,
// w as given (1; 0 for the float bits of a uint input, whose 1 was a denormal).
uint32_t DefaultSelectors(uint32_t n, uint32_t w) {
  return Selectors(n > 0 ? 0 : 4, n > 1 ? 1 : 4, n > 2 ? 2 : 4, n > 3 ? 3 : w);
}
uint32_t FetchDescriptor(uint32_t format, bool is_signed, bool integer, bool dec3n, uint32_t selectors) {
  return format | uint32_t(is_signed) << 6 | uint32_t(integer) << 7 | uint32_t(dec3n) << 8 | selectors << 9;
}

// The shader commit (821B6CE0) patches the vertex fetches of the bound VS by
// the declaration (+11820) and the stream strides (+12528) through 821B6018:
// in place when the last use of the microcode has completed (821B6BA0),
// otherwise on a copy loaded inline into the ring (821B68F0), which in the native
// mode is the discard buffer. 821B6018 reads the fetch templates kept in the
// VS object and writes 12-byte instructions only into the code it receives.
// The own draw runs it on an own copy of the microcode, so the fetch words are
// those of this draw on either path. Without declaration, or with container
// flag 0x40, the commit does not patch and neither does the own draw.
extern "C" void __imp__sub_821B6018(PPCContext& __restrict, uint8_t*);
bool PatchVertexFetches(PPCContext& ctx, uint8_t* base, uint32_t dev, BoundShader& vs, std::string& record) {
  const uint32_t decl = Get32(base, dev + 11820), flags = Get32(base, vs.container);
  if (!decl || (flags & 0x40)) {
    record = fmt::format("{{\"declaration\":{},\"container_flags\":{},\"patched\":false}}", decl, flags);
    return true;
  }
  if (!inline_draw.patch || vs.code.size() * 4 > kPatchBytes) return false;
  for (size_t i = 0; i < vs.code.size(); ++i) Put32(base, inline_draw.patch + uint32_t(4 * i), vs.code[i]);
  std::array<uint8_t, sizeof(PPCContext)> saved{};
  std::memcpy(saved.data(), &ctx, sizeof(PPCContext));
  ctx.r1.u64 = (ctx.r1.u32 - 1024) & ~15u;  // Below the caller's frame.
  ctx.r3.u64 = vs.object;
  ctx.r4.u64 = inline_draw.patch;
  ctx.r5.u64 = decl;
  ctx.r6.u64 = dev + 12528;
  ctx.r7.u64 = 0;  // Binding slot: 1 only without a pixel shader.
  __imp__sub_821B6018(ctx, base);
  std::memcpy(&ctx, saved.data(), sizeof(PPCContext));
  uint32_t changed = 0;
  for (size_t i = 0; i < vs.code.size(); ++i) {
    const uint32_t word = Get32(base, inline_draw.patch + uint32_t(4 * i));
    changed += word != vs.code[i];
    vs.code[i] = word;
  }
  record = fmt::format("{{\"declaration\":{},\"container_flags\":{},\"patched\":true,\"words_changed\":{}}}", decl,
                       flags, changed);
  return true;
}

struct VertexLayoutKey {
  uint32_t vs_object = 0, decl = 0, decl_id = 0;
  uint64_t vs_generation = 0;
  std::array<uint32_t, 4> strides{};
  bool operator==(const VertexLayoutKey& o) const {
    return vs_object == o.vs_object && decl == o.decl && decl_id == o.decl_id && vs_generation == o.vs_generation &&
           strides == o.strides;
  }
};
struct VertexLayoutKeyHash {
  size_t operator()(const VertexLayoutKey& k) const {
    const uint32_t words[] = {k.vs_object, k.decl, k.decl_id, uint32_t(k.vs_generation), uint32_t(k.vs_generation >> 32),
                              k.strides[0], k.strides[1], k.strides[2], k.strides[3]};
    return size_t(Fnv64(reinterpret_cast<const uint8_t*>(words), sizeof(words)));
  }
};
std::mutex vertex_layouts_mutex;
std::unordered_map<VertexLayoutKey, std::shared_ptr<const VertexLayout>, VertexLayoutKeyHash> vertex_layouts;
std::atomic<uint64_t> layouts_built{0}, layouts_reused{0}, layouts_uncached{0};
std::atomic<uint32_t> fetch_index_records{0};
// The title's instancing shaders (the 30 VS with g_vInstanceParams) compute,
// from the vertex index i, a mesh index x + i - instance * y with a
// multiply-add and an instance index (i + 0.5) / y with a multiply (x, y:
// g_vInstanceParams), and fetch the mesh with the first and the instance data
// with the second (floor, no fetch rounds it). The translated shaders read
// every element from the input assembler, so those streams become
// per-instance data of a D3D12 instanced draw (TranslateDraw).
//
// In 18 of them the mesh index is the index of the POSITION0 fetch: an
// offline pass over every vertex shader container of the title (prologue of
// the translated HLSL, fetch words of the microcode; 2026-10-18) found the
// multiply-add writing it, r0.x in 16, r1.y in vs-002256BC and r1.x in
// vs-00225B92. The other 12 fetch their mesh through a stream read at the mesh
// index (ClassifyIndirectStreams). A layout with fetch indices this cannot
// express fails, instead of drawing every stream per vertex.
void ClassifyInstanceStreams(uint8_t* base, const BoundShader& vs, const std::vector<uint32_t>& element_index,
                             VertexLayout& L) {
  constexpr uint32_t kVertexIndex = 0;  // r0.x as (register << 2 | component).
  uint32_t mesh_index = kVertexIndex;
  for (size_t i = 0; i < L.elements.size(); ++i)
    if (std::string_view(L.elements[i].semantic) == "POSITION" && L.elements[i].semantic_index == 0)
      mesh_index = element_index[i];
  if (std::all_of(element_index.begin(), element_index.end(), [&](uint32_t i) { return i == mesh_index; })) return;
  const int32_t params = FindFloatConstant(base, vs, "g_vInstanceParams");
  std::vector<int> slot_kind(L.stream_fetch.size(), -1);  // 0 per vertex, 1 per instance.
  uint32_t instance_index = UINT32_MAX;
  bool covered = params >= 0;
  for (size_t i = 0; covered && i < L.elements.size(); ++i) {
    const int kind = element_index[i] != mesh_index;
    if (kind && instance_index != UINT32_MAX && instance_index != element_index[i]) covered = false;
    if (kind) instance_index = element_index[i];
    int& slot = slot_kind[L.elements[i].slot];
    if (slot >= 0 && slot != kind) covered = false;
    slot = kind;
  }
  if (!covered) {
    std::string indices;
    for (const uint32_t i : element_index)
      indices += fmt::format("{}\"r{}.{}\"", indices.empty() ? "" : ",", i >> 2, "xyzw"[i & 3]);
    L.ok = false;
    L.reason = "vertex-fetch-index-uncovered";
    L.detail = fmt::format("\"instance_params\":{},\"indices\":[{}],\"elements\":[{}]", params, indices,
                           L.elements_record);
    return;
  }
  L.instance_params = params;
  for (const int kind : slot_kind) L.stream_per_instance.push_back(kind == 1);
  for (size_t i = 0; i < L.elements.size(); ++i) {
    if (element_index[i] == mesh_index) continue;
    L.elements[i].instance_step = 1;
    L.legacy_elements[i].instance_step = 1;
  }
}
// The role of a fetch in an indirect layout (FindIndirectFetches).
enum class FetchRole : uint8_t { kDirect, kSource, kIndirect };
// The 12 vertex shaders of the title that fetch their mesh through a stream:
// a k_32 POSITION1 read at the mesh index holds, per mesh vertex, the index of
// the vertex that POSITION0 and TEXCOORD0 (and COLOR1 in three of them) are
// fetched at, and the instance data is read at the instance index. An offline
// pass over the 2429 vertex shader containers and the translated HLSL
// (2026-10-19) found no other, and in each the POSITION1 value is overwritten
// before any use, so the input assembler may deliver anything for it. That
// cannot be proved at run time, so only these names take the layout.
constexpr std::array<std::string_view, 12> kIndirectShaders{
    "vs-00115146", "vs-0015E0E2", "vs-0019DDED", "vs-002114E7", "vs-0021241D", "vs-00224230",
    "vs-0023B4F8", "vs-002600E4", "vs-0026BCA5", "vs-0027699A", "vs-002C1CCF", "vs-00618CDC"};
// The indirect fetches of a block, in the patched order (the patch may move
// fetches, so the template order proves nothing): a fetch whose index is a
// register component an earlier fetch wrote. Covered when one source fetch,
// itself indexed by a value no fetch wrote, writes one component that indexes
// every indirect fetch, and the other fetches share one index no fetch wrote.
// Returns false when the block has indirect fetches outside that pattern;
// roles stays all kDirect when it has none.
template <class Fetches>
bool FindIndirectFetches(const Fetches& fetches, std::vector<FetchRole>& roles) {
  roles.assign(fetches.size(), FetchRole::kDirect);
  std::array<int, 64 * 4> writer;  // The fetch that last wrote each register component (register << 2 | component).
  writer.fill(-1);
  int source = -1, full_writer = -1;
  bool covered = true, relative = false;
  for (size_t i = 0; i < fetches.size(); ++i) {
    const auto& p = fetches[i];
    // A mini fetch takes the address of the full fetch before it.
    const int w = p.mini ? full_writer : writer[p.index & 0xFF];
    if (!p.mini) full_writer = w;
    if (w >= 0) {
      if (source < 0) source = w;
      covered = covered && w == source;
      roles[i] = FetchRole::kIndirect;
    }
    relative = relative || ((p.w0 >> 18) & 1);  // Destination relative to aL: unknown components.
    const uint32_t destination = (p.w0 >> 12) & 0x3F;
    for (unsigned c = 0; c < 4; ++c)
      if (((p.w1 >> (3 * c)) & 7) != 7) writer[destination << 2 | c] = int(i);
  }
  if (source < 0) return true;
  uint32_t written = 0;
  for (unsigned c = 0; c < 4; ++c) written += ((fetches[source].w1 >> (3 * c)) & 7) != 7;
  covered = covered && !relative && written == 1 && roles[source] == FetchRole::kDirect && !fetches[source].mini;
  roles[source] = FetchRole::kSource;
  uint32_t instance_index = UINT32_MAX;
  for (size_t i = 0; covered && i < fetches.size(); ++i) {
    if (roles[i] != FetchRole::kDirect) continue;
    if (instance_index != UINT32_MAX && fetches[i].index != instance_index) covered = false;
    instance_index = fetches[i].index;
  }
  return covered && instance_index != UINT32_MAX && instance_index != fetches[source].index;
}
// An indirect layout: the indirect elements are the mesh, per vertex at the
// source's values, which the draw binds as its index buffer; the direct ones
// are the instance data. The source keeps an input for the shader's signature
// on the first mesh slot, offset 0 (its value is unused, kIndirectShaders).
void ClassifyIndirectStreams(uint8_t* base, const BoundShader& vs, const std::vector<FetchRole>& element_role,
                             size_t source, VertexLayout& L) {
  const int32_t params = FindFloatConstant(base, vs, "g_vInstanceParams");
  std::vector<int> slot_kind(L.stream_fetch.size(), -1);  // 0 per vertex, 1 per instance.
  bool covered = params >= 0;
  for (size_t i = 0; covered && i < L.elements.size(); ++i) {
    if (i == source) continue;
    const int kind = element_role[i] == FetchRole::kDirect;
    int& slot = slot_kind[L.elements[i].slot];
    if (slot >= 0 && slot != kind) covered = false;
    slot = kind;
  }
  const auto mesh_slot = std::find(slot_kind.begin(), slot_kind.end(), 0);
  if (!covered || mesh_slot == slot_kind.end()) {
    L.ok = false;
    L.reason = "vertex-fetch-indirect-uncovered";
    L.detail = fmt::format("\"instance_params\":{},\"elements\":[{}]", params, L.elements_record);
    return;
  }
  L.instance_params = params;
  for (const int kind : slot_kind) L.stream_per_instance.push_back(kind == 1);
  const uint32_t slot = uint32_t(mesh_slot - slot_kind.begin());
  L.elements[source].slot = L.legacy_elements[source].slot = L.fetch[source].slot = slot;
  for (size_t i = 0; i < L.elements.size(); ++i) {
    if (element_role[i] != FetchRole::kDirect) continue;
    L.elements[i].instance_step = 1;
    L.legacy_elements[i].instance_step = 1;
  }
}
void ParseVertexElements(PPCContext& saved, uint8_t* base, uint32_t dev, const BoundShader& vs,
                         const CachedShader* cached, VertexLayout& L) {
  auto fail = [&](const char* reason, std::string detail) {
    L.ok = false;
    L.reason = reason;
    L.detail = std::move(detail);
  };
  BoundShader patched = vs;  // The fetch words of the bound declaration.
  if (!PatchVertexFetches(saved, base, dev, patched, L.patch_record)) return fail("vertex-fetch-patch-unproved", "\"detail\":null");
  L.patched_code = patched.code;
  const uint32_t shader = vs.container + Get32(base, vs.container + 0x18);
  const uint32_t field18 = Get32(base, shader + 0x18), count = Get32(base, shader + 0x1C);
  if (count > 16) return fail("vertex-elements-uncovered", fmt::format("\"count\":{}", count));
  uint32_t last_const = UINT32_MAX, last_stride = 0, last_index = 0;
  std::vector<std::pair<uint32_t, uint32_t>> ordered;  // (address, value)
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t value = Get32(base, shader + 0x24 + 4 * (field18 + i));
    ordered.emplace_back(value & 0xFFF, value);
  }
  std::sort(ordered.begin(), ordered.end());
  // The patch also sorts the fetches of the declaration by fetch
  // constant and offset (mini fetches for consecutive ones of a stream), so
  // the fetch at an element's address may be another element's (the Story
  // Mode buildings: COLOR0 read COLOR1, COLOR1 the normal, ...); each keeps
  // its destination register. The patched fetch of an element is the one
  // with the element's destination (bits 12..18 of word 0) in the template
  // microcode, whose order the shader cache follows; a mini fetch takes the
  // constant and stride of the full fetch before it, in the patched order.
  // The index of a fetch is a register component (src, bits 5..10, and its
  // swizzle, bits 30..31, of word 0); a mini fetch reuses the full fetch's.
  struct PatchedFetch {
    uint32_t address = 0, w0 = 0, w1 = 0, w2 = 0, fetch = 0, stride = 0, index = 0;
    bool mini = false;
  };
  std::vector<PatchedFetch> patched_fetches;
  for (const auto& [address, value] : ordered) {
    if (3 * address + 2 >= patched.code.size()) return fail("vertex-element-invalid", fmt::format("\"value\":{}", value));
    PatchedFetch p;
    p.address = address;
    p.w0 = patched.code[3 * address];
    p.w1 = patched.code[3 * address + 1];
    p.w2 = patched.code[3 * address + 2];
    p.mini = (p.w1 >> 30) & 1;
    p.fetch = p.mini ? last_const : ((p.w0 >> 20) & 0x1F) * 3 + ((p.w0 >> 25) & 3);
    p.stride = p.mini ? last_stride : p.w2 & 0xFF;
    p.index = p.mini ? last_index : ((p.w0 >> 5) & 0x3F) << 2 | (p.w0 >> 30);
    last_const = p.fetch;
    last_stride = p.stride;
    last_index = p.index;
    patched_fetches.push_back(p);
  }
  std::vector<FetchRole> roles;  // Per patched fetch.
  const std::string_view cache_name = cached ? std::string_view(cached->name) : std::string_view();
  if (!FindIndirectFetches(patched_fetches, roles))
    return fail("vertex-fetch-indirect-uncovered", fmt::format("\"cache\":\"{}\",\"pattern\":false", cache_name));
  if (std::find(roles.begin(), roles.end(), FetchRole::kSource) != roles.end() &&
      std::find(kIndirectShaders.begin(), kIndirectShaders.end(), cache_name) == kIndirectShaders.end())
    return fail("vertex-fetch-indirect-uncovered", fmt::format("\"cache\":\"{}\",\"pattern\":true", cache_name));
  const std::vector<uint32_t>& template_code = cached ? cached->code : vs.code;
  std::vector<uint32_t> element_index;  // The fetch index of each element, in the order of elements.
  std::vector<FetchRole> element_role;  // And its role in an indirect layout.
  size_t source_element = 0;
  for (const auto& [address, value] : ordered) {
    const uint32_t usage = (value >> 12) & 0xF, usage_index = (value >> 16) & 0xF;
    if (usage >= 14 || 3 * address + 2 >= template_code.size())
      return fail("vertex-element-invalid", fmt::format("\"value\":{}", value));
    const uint32_t destination = (template_code[3 * address] >> 12) & 0x7F;
    const PatchedFetch* match = nullptr;
    uint32_t matches = 0;
    for (const auto& p : patched_fetches)
      if (((p.w0 >> 12) & 0x7F) == destination) { match = &p; ++matches; }
    // Two fetches into one register (vs-001789C1: COLOR0 into r1.xyz and
    // COLOR1 into r1.w) are told apart by the components each writes (dst_swiz,
    // bits 0..11 of word 1, 7 = not written), which the patch keeps.
    if (matches > 1) {
      auto written = [](uint32_t w1) {
        uint32_t mask = 0;
        for (unsigned c = 0; c < 4; ++c)
          if (((w1 >> (3 * c)) & 7) != 7) mask |= 1u << c;
        return mask;
      };
      const uint32_t mask = written(template_code[3 * address + 1]);
      match = nullptr;
      matches = 0;
      for (const auto& p : patched_fetches)
        if (((p.w0 >> 12) & 0x7F) == destination && written(p.w1) == mask) { match = &p; ++matches; }
    }
    if (matches != 1)
      return fail("vertex-fetch-match-unproved", fmt::format("\"address\":{},\"destination\":{},\"matches\":{}", address,
                                                             destination, matches));
    const uint32_t w0 = match->w0, w1 = match->w1, w2 = match->w2, fetch = match->fetch, stride = match->stride;
    const bool mini = match->mini;
    const FetchRole role = roles[size_t(match - patched_fetches.data())];
    const bool source = role == FetchRole::kSource;
    element_index.push_back(match->index);
    element_role.push_back(role);
    const uint32_t xenos_format = (w1 >> 16) & 0x3F;
    const bool is_signed = (w1 >> 12) & 1, integer = (w1 >> 13) & 1;
    const bool half_pairs = xenos_format == 25 || xenos_format == 26 || xenos_format == 31 || xenos_format == 32;
    const bool halves = xenos_format == 31 || xenos_format == 32;
    // signed_rf_mode 1 (bit 14) and exp_adjust (bits 24..29, format/ucode.h)
    // stay a guard for the packed and 16-bit integer formats.
    const bool plain = !((w1 >> 14) & 1) && !((w1 >> 24) & 0x3F);
    // Swizzle selector 6 (undefined) stays a guard; selector 7 (kKeep)
    // reads 0 (BLENDWEIGHT 16_16_16_16 of the Story Mode skinned meshes).
    bool swizzle_covered = true;
    for (unsigned c = 0; c < 4; ++c) swizzle_covered = swizzle_covered && ((w1 >> (3 * c)) & 7) != 6;
    // The template shaders of the START policy: the typed formats; an element
    // they would need expanded on the CPU leaves them without a layout.
    const bool float_packed = !UintUsage(usage) && xenos_format == 16 && plain;
    const bool dec3 = xenos_format == 7 && plain;
    const bool swizzled_pairs = !UintUsage(usage) && usage != 5 && (xenos_format == 25 || xenos_format == 26) && plain &&
                                swizzle_covered;
    const uint32_t legacy = UintUsage(usage) ? (dec3 ? uint32_t(DXGI_FORMAT_R32G32B32A32_UINT) : UintVertexFormat(xenos_format))
                            : float_packed || swizzled_pairs || dec3 ? uint32_t(DXGI_FORMAT_R32G32B32A32_FLOAT)
                            : half_pairs && usage != 5 ? 0u
                            : VertexFormat(xenos_format, integer, is_signed);
    const bool legacy_expanded = (xenos_format == 16 && (float_packed || UintUsage(usage))) ||
                                 (UintUsage(usage) && half_pairs) || swizzled_pairs || dec3;
    // The decode of the element.
    VertexFetch vf;
    vf.address = address;
    vf.components = source ? 1 : FetchComponents(xenos_format);
    vf.patched_swizzle = w1 & 0xFFF;
    bool covered = vf.components != 0;
    uint32_t selectors = 0;
    bool dec3n = false;
    if (source) {
      // The index stream of an indirect layout, read by the draw as its index
      // buffer: k_32, unsigned, integer, one dword at offset 0 (below). The
      // shader's input of it is unused; vfetchNaruto decodes k_32 as zero.
      covered = xenos_format == 33 && !is_signed && integer && plain;
      selectors = DefaultSelectors(1, 4);
    } else if (UintUsage(usage)) {  // NORMAL, TANGENT, BINORMAL.
      if (dec3) {
        selectors = Selectors(0, 1, 2, 3);
      } else if (xenos_format == 16) {  // DEC3N (XenosRecomp's tfetchR11G11B10); w 0.
        dec3n = true;
        selectors = Selectors(0, 1, 2, 3);
      } else if (halves) {  // The first half of a pair is the high one after the 8in32 swap.
        vf.by_endian = true;
        selectors = xenos_format == 32 ? Selectors(0, 1, 2, 3) : Selectors(0, 1, 4, 5);
        vf.descriptor_8in32 = xenos_format == 32 ? Selectors(1, 0, 3, 2) : Selectors(1, 0, 4, 5);
      } else if (UintVertexFormat(xenos_format)) {  // The float bits.
        selectors = DefaultSelectors(vf.components, 4);
      } else {
        covered = false;
      }
    } else if (float_packed) {  // 10_11_11; w 1.
      selectors = Selectors(0, 1, 2, 5);
    } else if (dec3) {  // 2_10_10_10 (NORMAL of the Story Mode scene).
      selectors = Selectors(0, 1, 2, 3);
    } else if (usage == 5 && half_pairs) {  // TEXCOORD pairs: .yxwz after the 8in32 swap (tfetchTexcoord).
      vf.by_endian = true;
      vf.texcoord_index = int32_t(usage_index);
      selectors = DefaultSelectors(vf.components, 5);
      vf.descriptor_8in32 = xenos_format == 26 || xenos_format == 32 ? Selectors(1, 0, 3, 2) : Selectors(1, 0, 5, 4);
      covered = covered && legacy;
    } else if (swizzled_pairs || (halves && swizzle_covered)) {
      // The patched swizzle (POSITION 16_16, BLENDWEIGHT, 16_16_FLOAT and
      // 16_16_16_16_FLOAT in COLOR of the Story Mode scene).
      vf.resolve_swizzle = true;
    } else if (!half_pairs && legacy) {
      // The patched swizzle too (the D3DCOLOR elements of the Story Mode
      // buildings, 8_8_8_8 with Z, Y, X, W).
      selectors = DefaultSelectors(vf.components, 5);
      vf.resolve_swizzle = swizzle_covered;
    } else {
      covered = false;
    }
    vf.descriptor = FetchDescriptor(xenos_format, is_signed, integer, dec3n, selectors);
    vf.descriptor_8in32 = vf.by_endian ? FetchDescriptor(xenos_format, is_signed, integer, dec3n, vf.descriptor_8in32)
                                       : vf.descriptor;
    const uint32_t offset = (w2 >> 8) & 0x7FFFFF, bytes = source ? 4 : FetchBytes(xenos_format);
    const uint32_t format = covered ? RawVertexFormat(bytes) : 0;
    L.elements_record += fmt::format("{}{{\"usage\":{},\"index\":{},\"address\":{},\"fetch\":{},\"xenos_format\":{},"
        "\"dxgi\":{},\"legacy_dxgi\":{},\"descriptor\":{},\"stride_words\":{},\"offset_words\":{},\"mini\":{},"
        "\"fetch_address\":{},\"words\":[{},{},{}]}}", L.elements_record.empty() ? "" : ",", usage, usage_index, address,
        fetch, xenos_format, format, legacy, vf.descriptor, stride, offset, mini, match->address, w0, w1, w2);
    if ((w0 & 0x1F) != 0 || fetch >= 96 || !stride || !format)
      return fail("vertex-fetch-uncovered", fmt::format("\"elements\":[{}]", L.elements_record));
    if (4 * offset + bytes > 4 * stride)
      return fail("vertex-element-beyond-stride", fmt::format("\"elements\":[{}]", L.elements_record));
    if (source) {
      if (stride != 1 || offset)
        return fail("vertex-fetch-indirect-uncovered", fmt::format("\"elements\":[{}]", L.elements_record));
      // No stream slot of its own (ClassifyIndirectStreams).
      L.indirect_fetch = int32_t(fetch);
      source_element = L.elements.size();
      L.elements.push_back({kSemantics[usage], usage_index, format, 0, 0});
      L.legacy_elements.push_back({kSemantics[usage], usage_index, legacy, 0, 0});
      L.legacy_ok = false;
      L.fetch.push_back(vf);
      continue;
    }
    auto slot_it = std::find(L.stream_fetch.begin(), L.stream_fetch.end(), fetch);
    uint32_t slot = uint32_t(slot_it - L.stream_fetch.begin());
    if (slot_it == L.stream_fetch.end()) {
      L.stream_fetch.push_back(fetch);
      L.stream_strides.push_back(stride * 4);
    } else if (L.stream_strides[slot] != stride * 4) {
      return fail("vertex-stride-conflict", fmt::format("\"elements\":[{}]", L.elements_record));
    }
    vf.slot = slot;
    L.elements.push_back({kSemantics[usage], usage_index, format, slot, offset * 4});
    L.legacy_elements.push_back({kSemantics[usage], usage_index, legacy, slot, offset * 4});
    L.legacy_ok = L.legacy_ok && legacy && !legacy_expanded;
    L.fetch.push_back(vf);
  }
  if (L.indirect_fetch >= 0) ClassifyIndirectStreams(base, vs, element_role, source_element, L);
  else ClassifyInstanceStreams(base, vs, element_index, L);
}
// The cached DXIL comes from the template microcode of the SDB, whose fetch
// keeps the identity destination swizzle (0x688); the runtime patch 821B6018
// writes the swizzle of the declaration, (Y, X, 0, 1) for a pair. The decode
// feeds the input components so that the template fetch (its destination
// swizzle t) yields what the patched fetch (swizzle p) writes: output i reads
// input t[i], so input t[i] takes p[i] (identity template: the patched swizzle
// itself). Any template, as the (X, Y, Z, keep) template under the (Y, X,
// W, keep) patch of the Story Mode skinned meshes. A template constant or keep
// must match the patch; two outputs reading one input with different
// selectors, or selector 6, stay a guard. Components the format lacks, and
// keep, read 0, as in the interpreter.
void ResolveTemplateSwizzles(VertexLayout& L, const CachedShader& cached) {
  for (auto& vf : L.fetch) {
    if (!vf.resolve_swizzle) continue;
    const uint32_t template_swizzle = 3 * vf.address + 1 < cached.code.size()
                                          ? cached.code[3 * vf.address + 1] & 0xFFF : UINT32_MAX;
    std::array<uint32_t, 4> input{4, 4, 4, 4};  // Unread inputs: 0.
    std::array<bool, 4> set{};
    bool covered = template_swizzle != UINT32_MAX;
    for (unsigned i = 0; covered && i < 4; ++i) {
      const uint32_t ts = (template_swizzle >> (3 * i)) & 7, ps = (vf.patched_swizzle >> (3 * i)) & 7;
      if (ts == 6) covered = false;
      else if (ts >= 4) covered = ts == ps;
      else if (set[ts]) covered = input[ts] == ps;
      else { input[ts] = ps; set[ts] = true; }
    }
    if (!covered) {
      L.ok = false;
      L.reason = "vertex-template-swizzle-uncovered";
      L.detail = fmt::format("\"elements\":[{}],\"template_swizzle\":{}", L.elements_record, template_swizzle);
      return;
    }
    std::array<uint32_t, 4> sources{};
    for (unsigned i = 0; i < 4; ++i)
      sources[i] = input[i] < 4 ? (input[i] < vf.components ? input[i] : 4) : input[i] == 5 ? 5 : 4;
    const uint32_t selectors = Selectors(sources[0], sources[1], sources[2], sources[3]);
    vf.descriptor = (vf.descriptor & 0x1FF) | selectors << 9;
    vf.descriptor_8in32 = vf.descriptor;
  }
}
// The layout of the bound pair: reused, or built (parse, the tolerant
// identification of a VS the exact one missed, the template swizzles) and
// kept when the pair has a stable key. vs_cached, candidates and differing
// are updated by the tolerant identification.
std::shared_ptr<const VertexLayout> FindOrBuildVertexLayout(PPCContext& saved, uint8_t* base, uint32_t dev,
                                                            ShaderEntry* entry, const BoundShader& vs, bool quad,
                                                            CachedShader*& vs_cached, uint32_t& candidates,
                                                            uint32_t& differing) {
  VertexLayoutKey key;
  key.vs_object = vs.object;
  key.vs_generation = entry ? entry->generation : 0;
  key.decl = Get32(base, dev + 11820);
  key.decl_id = key.decl ? Get32(base, key.decl + 48) : 0;
  for (unsigned i = 0; i < 4; ++i) key.strides[i] = Get32(base, dev + 12528 + 4 * i);
  const bool no_patch = !key.decl || (Get32(base, vs.container) & 0x40);
  const bool cacheable = entry && (no_patch || key.decl_id) && !quad;
  if (cacheable) {
    std::lock_guard lock(vertex_layouts_mutex);
    auto it = vertex_layouts.find(key);
    if (it != vertex_layouts.end()) { ++layouts_reused; return it->second; }
  }
  auto layout = std::make_shared<VertexLayout>();
  // The identification of an object without its entry runs before the parse.
  // It is by hash, so a VS whose fetch words are patched is not identified
  // (identified_tolerant stays 0).
  if (!vs_cached && vs.object) {
    vs_cached = IdentifyShader(vs, true, candidates, differing);
    if (vs_cached) ++shader_identified_tolerant;
    if (entry) { entry->cached = vs_cached; entry->candidates = candidates; entry->differing = differing; }
  }
  ParseVertexElements(saved, base, dev, vs, vs_cached, *layout);
  if (layout->ok && vs_cached) ResolveTemplateSwizzles(*layout, *vs_cached);
  // The request shares the elements and carries their id.
  auto elements_id = [](const std::vector<VertexElement>& elements) {
    uint64_t id = 0xCBF29CE484222325ull;
    auto mix = [&](const void* data, size_t size) {
      const auto* p = static_cast<const uint8_t*>(data);
      for (size_t i = 0; i < size; ++i) id = (id ^ p[i]) * 0x100000001B3ull;
    };
    for (const auto& e : elements) {
      mix(e.semantic, std::strlen(e.semantic));
      const uint32_t fields[4] = {e.semantic_index, e.format, e.slot, e.offset};
      mix(fields, sizeof(fields));
      if (e.instance_step) mix(&e.instance_step, sizeof(e.instance_step));  // As the engine's ElementsId.
    }
    return id;
  };
  layout->shared_elements = std::make_shared<const std::vector<VertexElement>>(layout->elements);
  layout->shared_legacy_elements = std::make_shared<const std::vector<VertexElement>>(layout->legacy_elements);
  layout->elements_id = elements_id(layout->elements);
  layout->legacy_elements_id = elements_id(layout->legacy_elements);
  if (cacheable && vs_cached) {
    std::lock_guard lock(vertex_layouts_mutex);
    vertex_layouts[key] = layout;
    ++layouts_built;
  } else {
    ++layouts_uncached;
  }
  return layout;
}
size_t VertexLayoutCount() {
  std::lock_guard lock(vertex_layouts_mutex);
  return vertex_layouts.size();
}
}  // namespace gpu
