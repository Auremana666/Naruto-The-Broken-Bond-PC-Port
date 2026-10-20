#pragma once

#include <cstddef>
#include <cstdint>

// Precompiled pipelines of the D3D boundary and the record of the
// pipelines created (shaders/pipelines.cpp).
namespace gpu {
// The precompile pool, started at the first draw.
void StartPipelinePrecompileOnce();
// The pipelines the list knows for a world (common/title_loading.h), posted to
// the asynchronous threads when the title starts loading it: how many the list
// has for the key, how many were posted, how many the set had already and how
// many could not be resolved. Any thread; nothing before the pool started.
struct WorldPrefetch {
  size_t listed = 0, posted = 0, present = 0, unresolved = 0;
};
WorldPrefetch PrefetchWorldPipelines(uint32_t world_key);
// The pipelines created with their stable identities, also as
// pipelines.json in REX_NATIVE_FRAME_DIR, on the stop and on the close.
void RecordPipelines();
}  // namespace gpu
