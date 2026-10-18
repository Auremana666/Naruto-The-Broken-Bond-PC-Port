#pragma once

// Precompiled pipelines of the D3D boundary and the record of the
// pipelines created (shaders/pipelines.cpp).
namespace gpu {
// The precompile pool, started at the first draw.
void StartPipelinePrecompileOnce();
// The pipelines created with their stable identities, also as
// pipelines.json in REX_NATIVE_FRAME_DIR, on the stop and on the close.
void RecordPipelines();
}  // namespace gpu
