// The stable identities of a pipeline, the same for every backend: the id of
// its input layout, its triple (VS, PS, layout) and its key. They name the
// pipelines of the embedded list (shaders/pipelines.cpp) from boot to boot, so
// what they hash is fixed: a change of representation versions the list.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "gpu/backend/contracts.h"

namespace gpu {

// The FNV of the elements (semantic, semantic index, format, slot, offset).
// Per-instance elements append their nonzero instance step; per-vertex IDs
// remain compatible with the existing Broken Bond pipeline list.
uint64_t ElementsId(const std::vector<VertexElement>& elements);

// The part of the key the driver compiles shaders for.
uint64_t TripleKey(const std::string& vs_key, const std::string& ps_key, uint64_t layout_id);

// The key of a pipeline: its shader names (with the spec suffix of the pixel
// shader), its layout id and its state words (DrawStateWords).
uint64_t PipelineKey(const std::string& vs_key, const std::string& ps_key, uint64_t layout_id,
                     const std::array<uint32_t, 21>& state);

// State word 10: the class of the topology. Schema 1 has the triangles only,
// 3 (the value of D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE when the list began).
inline constexpr uint32_t kTopologyClassTriangle = 3;

// The state words of a draw's pipeline key from the request and its target:
// the samples of the target, and whether a depth and a color target are bound.
// Words: 0 RB_BLENDCONTROL, 1 color mask, 2 depth enable, 3 depth write,
// 4 depth function, 5 cull front, 6 cull back, 7 front CCW, 8 samples, 9 depth
// target, 10 topology class, 11 alpha to coverage, 12 stencil enable, 13 front
// face, 14 back face, 15 read mask, 16 write mask, 17 depth only, 18 color
// target, 19 depth bias, 20 slope (float bits). False: a topology the draw
// does not cover.
bool DrawStateWords(const DrawRequest& r, uint32_t samples, bool depth, bool color, std::array<uint32_t, 21>& w);

}  // namespace gpu
