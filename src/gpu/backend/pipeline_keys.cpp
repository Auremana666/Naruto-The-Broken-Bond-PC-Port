// The stable identities of a pipeline (pipeline_keys.h).
#include "gpu/backend/pipeline_keys.h"

#include <bit>
#include <cstring>
#include "gpu/common/hash.h"

namespace gpu {

uint64_t ElementsId(const std::vector<VertexElement>& elements) {
  uint64_t id = 0xCBF29CE484222325ull;
  for (const auto& e : elements) {
    id = Fnv64(reinterpret_cast<const uint8_t*>(e.semantic), std::strlen(e.semantic), id);
    const uint32_t fields[4] = {e.semantic_index, uint32_t(e.format), e.slot, e.offset};
    id = Fnv64(reinterpret_cast<const uint8_t*>(fields), sizeof(fields), id);
    if (e.instance_step)
      id = Fnv64(reinterpret_cast<const uint8_t*>(&e.instance_step), sizeof(e.instance_step), id);
  }
  return id;
}

uint64_t TripleKey(const std::string& vs_key, const std::string& ps_key, uint64_t layout_id) {
  uint64_t key = 0xCBF29CE484222325ull;
  key = Fnv64(reinterpret_cast<const uint8_t*>(vs_key.data()), vs_key.size(), key);
  key = Fnv64(reinterpret_cast<const uint8_t*>("|"), 1, key);
  key = Fnv64(reinterpret_cast<const uint8_t*>(ps_key.data()), ps_key.size(), key);
  return Fnv64(reinterpret_cast<const uint8_t*>(&layout_id), sizeof(layout_id), key);
}

uint64_t PipelineKey(const std::string& vs_key, const std::string& ps_key, uint64_t layout_id,
                     const std::array<uint32_t, 21>& state) {
  uint64_t key = 0xCBF29CE484222325ull;
  key = Fnv64(reinterpret_cast<const uint8_t*>(vs_key.data()), vs_key.size(), key);
  key = Fnv64(reinterpret_cast<const uint8_t*>("|"), 1, key);
  key = Fnv64(reinterpret_cast<const uint8_t*>(ps_key.data()), ps_key.size(), key);
  key = Fnv64(reinterpret_cast<const uint8_t*>(&layout_id), sizeof(layout_id), key);
  return Fnv64(reinterpret_cast<const uint8_t*>(state.data()), sizeof(uint32_t) * state.size(), key);
}

bool DrawStateWords(const DrawRequest& r, uint32_t samples, bool depth, bool color, std::array<uint32_t, 21>& w) {
  const bool triangles = r.topology == Topology::kTriangleList || r.topology == Topology::kTriangleStrip;
  w = {r.blend_control, r.color_mask, r.depth_enable, r.depth_write, r.depth_func, r.cull_front, r.cull_back,
       r.front_ccw, samples, depth, triangles ? kTopologyClassTriangle : 0, r.alpha_to_coverage, r.stencil_enable,
       r.stencil_front, r.stencil_back, r.stencil_read_mask, r.stencil_write_mask, r.depth_only, color,
       uint32_t(r.depth_bias), std::bit_cast<uint32_t>(r.depth_bias_slope)};
  return triangles;
}

}  // namespace gpu
