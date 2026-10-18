#pragma once

// Vertex input of the D3D boundary (vertex_layout.cpp): the Xenos vertex
// formats, the vertex fetch patch of the bound VS and the layouts translated
// once per (VS object, declaration id, stream strides).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <rex/ppc/context.h>
#include "gpu/backend/engine.h"
#include "gpu/shaders/shaders.h"

namespace gpu {
// The vertex input of a draw, translated once per (VS object, declaration
// id, stream strides). The D3D of the title keys its vfetch patch (821B6BA0) on
// the same triple: the declaration id at decl+48 (a global counter) and the 16
// stride bytes at device+12528 masked by the declaration. The parse runs over
// a patched copy of the microcode; a failed parse is kept with its guard. A
// declaration without id (first use through the ring copy of
// 821B68F0) or an object without entry is parsed per draw, counted.
// The descriptor of one element, in fetch address order.
struct VertexFetch {
  uint32_t descriptor = 0, descriptor_8in32 = 0;  // The second when the stream is 8in32 (by_endian).
  uint32_t slot = 0, address = 0, components = 0, patched_swizzle = 0;
  bool by_endian = false;        // 16-bit pairs whose order follows the stream endian (8in16 or 8in32 only).
  bool resolve_swizzle = false;  // Sources from the patched swizzle (ResolveTemplateSwizzles).
  int32_t texcoord_index = -1;   // TEXCOORD pairs: the bit of g_SwappedTexcoords of the legacy shaders.
};
struct VertexLayout {
  bool ok = true;
  std::string reason, detail;  // The guard when !ok.
  std::vector<VertexElement> elements;         // Raw R32*_UINT, decoded by vfetchNaruto.
  std::vector<VertexElement> legacy_elements;  // The typed formats of the template shaders (START policy (a)).
  bool legacy_ok = true;                             // No element the legacy shaders needed expanded.
  // The same elements shared with every request of the pair, and their
  // stable ids (FNV of the elements), the layout part of the pipeline key.
  VertexElements shared_elements, shared_legacy_elements;
  uint64_t elements_id = 0, legacy_elements_id = 0;
  std::vector<VertexFetch> fetch;                    // Per element, in the order of elements.
  std::vector<uint32_t> stream_strides;  // Bytes, per slot.
  std::vector<uint32_t> stream_fetch;    // Fetch constant index of each stream slot.
  // The VS float constant g_vInstanceParams (x: first mesh vertex, y: vertices
  // per instance) when elements read per-instance data; -1 otherwise.
  int32_t instance_params = -1;
  std::vector<bool> stream_per_instance;  // Per slot, with instance_params.
  std::vector<uint32_t> patched_code;  // The microcode with the fetch words of the declaration (START policy).
  std::string elements_record, patch_record;
};
// The layout of the bound pair: reused, or built (parse, the tolerant
// identification of a VS the exact one missed, the template swizzles) and
// kept when the pair has a stable key.
std::shared_ptr<const VertexLayout> FindOrBuildVertexLayout(PPCContext& saved, uint8_t* base, uint32_t dev,
                                                            ShaderEntry* entry, const BoundShader& vs, bool quad,
                                                            CachedShader*& vs_cached, uint32_t& candidates,
                                                            uint32_t& differing);
size_t VertexLayoutCount();
extern std::atomic<uint64_t> layouts_built, layouts_reused, layouts_uncached;
}  // namespace gpu
