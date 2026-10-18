#pragma once

#include <memory>
#include <string>
#include <array>
#include <cstdint>
#include <vector>
#include "gpu/settings.h"

namespace gpu {
// Resource layer of the D3D boundary. Keys are guest header
// addresses of XDK D3D resources; formats are the host equivalents.
// Sizes, rectangles, the viewport and sample counts of every request are
// the guest's; the engine maps them to its own surfaces by the RenderMode of
// Initialize.
enum class SurfaceFormat : uint32_t { Color8888, DepthStencil };
struct SurfaceDesc {
  uint32_t key = 0, width = 0, height = 0, guest_format = 0, samples = 1;
  SurfaceFormat format = SurfaceFormat::Color8888;
};
// One clear of the bound surfaces (D3DDevice_ClearF / BeginTiling semantics).
// Zero keys are unbound slots. An empty rect (right <= left) means the whole surface.
struct ClearRequest {
  std::array<uint32_t, 4> color_keys{};
  uint32_t depth_key = 0;
  bool clear_color = false, clear_depth = false, clear_stencil = false;
  std::array<float, 4> color{};
  float depth = 1.0f;
  uint8_t stencil = 0;
  int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// D3DDevice_Resolve of one bound surface into a guest texture header.
struct ResolveRequest {
  uint32_t source_key = 0, dest_key = 0, dest_width = 0, dest_height = 0, dest_format = 0;
  bool has_rect = false;
  uint32_t left = 0, top = 0, right = 0, bottom = 0, dest_x = 0, dest_y = 0;
  // Source sample (RB_COPY_CONTROL.copy_sample_select k0..k3), or -1 for all
  // samples: their minimum for a depth source, their average for a color one.
  int32_t sample = -1;
};

// One own indexed draw (D3DDevice_DrawIndexedVertices semantics).
// Shaders are XenosRecomp DXIL with its binding layout: textures t0 in spaces
// 0..2, samplers s0 space3, constants b0 (VS), b1 (PS) and b2 (shared) in
// space4. Data is already little endian; textures are already linear. Xenos
// register values are translated by the engine: RB_BLENDCONTROL0,
// RB_COLOR_MASK and the depth/cull fields.
struct VertexElement {
  const char* semantic = "POSITION";  // Static string (XenosRecomp semantics).
  uint32_t semantic_index = 0, format = 0, slot = 0, offset = 0;  // format: DXGI_FORMAT
  // 1: per-instance data, the stream the title's instancing shaders fetch
  // with the instance index; 0: per-vertex data.
  uint32_t instance_step = 0;
};
struct VertexStream {
  std::vector<uint8_t> data;  // Inline data of the draw (key 0).
  uint32_t stride = 0;
  // A buffer the engine keeps (UploadBuffer), bound as the view of
  // bytes at offset; data stays empty.
  uint64_t key = 0;
  uint32_t offset = 0, bytes = 0;
};
// One upload of a buffer the engine keeps (vertex or index data,
// already little endian), keyed by the caller. Created at its first upload,
// whose single range covers it; later uploads copy their ranges in the open
// batch, before the draws recorded after them. A version not above the
// buffer's is skipped. Released by ReleaseBuffer once its last use completed.
struct BufferUpload {
  uint64_t key = 0, version = 0;
  uint32_t bytes = 0;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> ranges;  // (offset, data)
};
// One sampled 2D texture of a draw. The engine keeps one own copy
// per key and refreshes it when the layout or the version changes; empty data
// means the caller knows the copy is current. The texture layer uploads
// a copy with UploadSampled and the draw carries no data; versions grow, and
// a draw takes the copy at its version or a later one. A resolve destination
// is sampled from the own texture of its header. The descriptor indices of the
// sampler register are written into the shared constants (+4r view, +192+4r
// sampler).
struct DrawTexture {
  uint32_t reg = 0, key = 0;
  bool resolved = false;
  bool cube = false;  // A cubemap, six array slices; data face after face (level + face * levels).
  uint32_t format = 0, width = 0, height = 0, levels = 1;  // DXGI_FORMAT
  uint64_t layout = 0, version = 0;
  std::vector<std::vector<uint8_t>> data;  // Level rows packed (blocks for BC formats).
  uint32_t component_mapping = 0x1688;     // D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING
  uint32_t filter = 0, address_u = 1, address_v = 1, address_w = 1, max_anisotropy = 1;  // D3D12 values
  float lod_bias = 0, min_lod = 0, max_lod = 0;
  std::array<float, 4> border_color{};  // D3D12_TEXTURE_ADDRESS_MODE_BORDER color.
};
// The input layout of a draw, shared by every draw of the pair (VS,
// declaration): the elements and their stable id (FNV of the elements), part
// of the pipeline key.
using VertexElements = std::shared_ptr<const std::vector<VertexElement>>;
struct DrawRequest {
  std::string vs_key, ps_key;
  std::vector<DrawTexture> textures;
  std::shared_ptr<const std::vector<uint8_t>> vs_dxil, ps_dxil;
  VertexElements elements;
  uint64_t layout_id = 0;  // 0: the engine hashes the elements (tests, the quad).
  std::vector<VertexStream> streams;
  std::vector<uint8_t> indices;  // Inline indices of the draw (index_key 0).
  bool index32 = false;
  // The index buffer the engine keeps, read from index_offset.
  uint64_t index_key = 0;
  uint32_t index_offset = 0;
  uint32_t index_count = 0, topology = 4;  // D3D_PRIMITIVE_TOPOLOGY
  int32_t base_vertex = 0;
  uint32_t instance_count = 1;  // index_count indices per instance.
  // The constant blocks live in the engine (one 4 KB block each for the
  // VS and the PS). A request
  // carries either the whole block (4096 bytes in vs_constants / ps_constants:
  // the evidence draws, the quad, the START lot) or only the registers written
  // since the last request (vs_constants_delta / ps_constants_delta: records of
  // uint16 first register, uint16 count, count x 16 bytes), applied in order on
  // the owner. Empty both: nothing changed.
  std::vector<uint8_t> vs_constants, ps_constants;
  std::vector<uint8_t> vs_constants_delta, ps_constants_delta;
  // Version of each constant block (0: always uploaded); the engine
  // rebinds the CBV of the previous draw of the batch when the version holds.
  uint64_t vs_constants_version = 0, ps_constants_version = 0;
  std::array<uint8_t, 512> shared_constants{};
  // A posted draw of the batch needs no evidence string; the engine
  // formats it only on failure. Evidence draws (Run) and the tests keep it.
  bool want_evidence = true;
  // A pipeline that is not ready is created asynchronously and this draw
  // records everything but itself (batched production draws only; the
  // evidence draws, the quad and the START policy keep the synchronous take),
  // unless the triple (VS, PS, layout) is warm and the frame's budget allows:
  // then the draw creates it in place (Engine::State::TakeAsync).
  bool async_pipeline = false;
  uint32_t color_key = 0, depth_key = 0;
  // No pixel shader (the title's RB_MODECONTROL depth only): ps_dxil null,
  // no color written; color_key may be 0 (depth target only).
  bool depth_only = false;
  uint32_t blend_control = 0x00010001, color_mask = 0xF;
  bool alpha_to_coverage = false;  // RB_COLORCONTROL alpha-to-mask
  std::array<float, 4> blend_factor{};
  bool depth_enable = false, depth_write = false;
  uint32_t depth_func = 7;  // Xenos: 0 never .. 7 always.
  // Xenos stencil faces: function | fail << 3 | zpass << 6 | zfail << 9 (bits 8..19
  // or 20..31 of RB_DEPTHCONTROL); ref and masks of RB_STENCILREFMASK.
  bool stencil_enable = false;
  uint32_t stencil_front = 7, stencil_back = 7, stencil_ref = 0, stencil_read_mask = 0xFF, stencil_write_mask = 0xFF;
  bool cull_front = false, cull_back = false, front_ccw = true;
  // Polygon offset as the D3D12 rasterizer depth bias (Xenia's
  // GetD3D10IntegerPolygonOffset and the subpixel slope unit).
  int32_t depth_bias = 0;
  float depth_bias_slope = 0;
  std::array<float, 6> viewport{};  // x, y, width, height, min z, max z
  std::array<int32_t, 4> scissor{};  // left, top, right, bottom
};

// One pipeline of the precompiled set, by the stable identities of
// its key (PipelineList): the shader names of the cache and their DXIL (no PS
// when ps_key is "depth-only"), the input layout and its id, and the state
// words of DrawIndexed (blend, color mask, depth, cull, target samples and
// depth and color presence, topology, alpha to coverage, stencil, bias).
struct PipelineDesc {
  std::string vs_key, ps_key;
  std::shared_ptr<const std::vector<uint8_t>> vs_dxil, ps_dxil;
  VertexElements elements;
  uint64_t layout_id = 0;
  std::array<uint32_t, 21> state{};
};

// How the swapchain presents (gpu/settings.h). With VSync the Present
// waits for the vblank (interval 1); without it the Present returns at once and
// tears only when tearing is allowed (VRR in borderless fullscreen). The title's
// time does not depend on it: the Swap limiter paces the frames.
struct PresentMode {
  bool vsync = true, allow_tearing = true;
  uint32_t buffers = 2, max_frame_latency = 1;
  // The brightness on the title's ramp (0.5 leaves it).
  float brightness = 0.5f;
  // The output effect of the SDK's presenter and its FXAA at the Swap.
  PresentEffect effect = PresentEffect::kBilinear;
  SwapPostEffect post_effect = SwapPostEffect::kNone;
  float cas_sharpness = 0.0f, fsr_sharpness_reduction = 0.2f;
  uint32_t fsr_passes = 4;
};

// How the engine renders the title's targets, fixed for its life:
// the internal resolution scale, and the sample count of the targets the title
// creates with MSAA (0 keeps the title's; 2, 4 or 8, lowered at Initialize to
// what the device supports). Single-sample targets stay single-sample. The
// device: the adapter by its DXGI description; empty, or a name not found,
// keeps Plume's choice (the most dedicated video memory).
struct RenderMode {
  ResolutionScale scale;
  uint32_t msaa_samples = 0;
  std::string device;
  uint32_t Samples(uint32_t guest) const { return guest > 1 && msaa_samples ? msaa_samples : guest; }
};

// Host engine owned for the entire accepted guest segment. No guest commands,
// read pointers, vblank or interrupts are implemented here; presentation is
// the own swapchain.
class Engine final {
 public:
  Engine();
  ~Engine();
  bool Initialize(std::string& evidence, const RenderMode& mode = RenderMode{});
  // The adapter in use (its DXGI description), after Initialize.
  std::string DeviceName() const;
  bool Shutdown(std::string& evidence);
  bool InitializeFrontend(uint64_t session_generation, std::string& evidence);
  bool VerifyFrontend(uint64_t session_generation, std::string& evidence);
  bool ReleaseFrontend(uint64_t session_generation, std::string& evidence);
  // Own render-target and depth surfaces, created on the owner thread. A
  // surface is destroyed only when no submitted work can still reference it.
  bool CreateSurface(const SurfaceDesc& desc, std::string& evidence);
  bool DestroySurface(uint32_t key, std::string& evidence);
  // Keeps a surface with exactly this description, recreating it when idle.
  bool EnsureSurface(const SurfaceDesc& desc, std::string& evidence);
  // Submits without waiting; the surfaces record the operation fence.
  bool ClearSurfaces(const ClearRequest& request, uint64_t& fence, std::string& evidence);
  // Waits for every submitted operation (BlockOnFence for own work).
  bool WaitIdle(std::string& evidence);
  // Closes the open batch without waiting; the host event (a HANDLE)
  // is set once every operation submitted so far has completed (target is the
  // fence it waits for). The frame lot of D3DDevice_Swap.
  bool CloseFrame(void* event, uint64_t& target, std::string& evidence);
  // Presentation: own Plume swapchain (B8G8R8A8_UNORM; buffers and frame
  // latency from the mode) on the window (an HWND), created on the engine queue.
  bool AttachWindow(void* window, std::string& evidence, const PresentMode& mode = PresentMode{});
  // Waits for every submitted operation, then releases the swapchain.
  bool DetachWindow(std::string& evidence);
  // The title's gamma ramp: D3DGAMMARAMP words (red, green, blue x 256; the
  // 10-bit value << 6). Used by every later Present until replaced.
  bool SetGammaRamp(const std::array<uint16_t, 768>& words, std::string& evidence);
  bool SetVsync(bool enabled, std::string& evidence);
  // Copies the RGBA8 image of key (an own texture with defined content, or a
  // single-sample color surface) into a stage slot, in the open batch. A front
  // buffer with undefined content is refused. The slot must have been
  // presented since it was last staged.
  bool StageFront(uint32_t key, uint32_t& slot, std::string& evidence);
  // Presents a staged slot: black bars, the image scaled into the largest
  // rect of its aspect, the gamma ramp applied, then the swapchain Present.
  // The swapchain follows the window size. A minimized (empty) window skips
  // the frame. With capture, the back buffer is read back after the pass
  // (BGRA rows, its size in width and height) and the call waits for it.
  bool Present(uint32_t slot, bool capture, std::vector<uint8_t>& bgra, uint32_t& width, uint32_t& height,
               std::string& evidence);
  // Live own resources (textures of guest headers, surfaces, stage slots).
  std::string ResourceCounts();
  // Releases the own texture of a guest header (a resolve
  // destination) and its SRV slot once no submitted work can use them: at once
  // when its last use completed, otherwise retired with its fence and released
  // by Reclaim, WaitIdle or Shutdown. A header without an own texture is fine.
  bool ReleaseTexture(uint32_t key, std::string& evidence);
  // Releases the sampled copy of guest texture memory at a base
  // address (the key of a draw texture that is not a resolve destination) and
  // its SRV slot, as ReleaseTexture does: at once when its last use completed,
  // otherwise retired with its fence. An address without a copy is fine.
  bool ReleaseSampled(uint32_t address, std::string& evidence);
  // Ordered close: releases the own surfaces and textures no guest
  // header of the registry owns (pass backings, resolve destinations of headers
  // the title did not create through D210) and every remaining sampled
  // copy, as DestroySurface, ReleaseTexture and ReleaseSampled do, then waits
  // for the queue so nothing stays retired.
  bool ReleaseRemaining(std::string& evidence);
  // Copies or resolves a surface into the own texture of a guest header.
  bool ResolveSurface(const ResolveRequest& request, uint64_t& fence, std::string& evidence);
  // Draws into an own surface with an own pipeline; submits without waiting.
  bool DrawIndexed(const DrawRequest& request, uint64_t& fence, std::string& evidence);
  // The pipelines created so far, one JSON object each, with the stable
  // identities of their key (shader names, layout id and elements, state
  // words): the list a build step can turn into the precompiled set.
  std::string PipelineList();
  // The key of a pipeline from its stable identities, as DrawIndexed
  // computes it; and the created pipelines as descriptions (no DXIL).
  static uint64_t PipelineKey(const std::string& vs_key, const std::string& ps_key, uint64_t layout_id,
                              const std::array<uint32_t, 21>& state);
  std::vector<PipelineDesc> PipelineDescs();
  // Precompiled pipelines. PreparePipelines (owner) creates the draw root
  // signature and heaps once. PrecompilePipeline then runs on any thread,
  // concurrently with the owner: it creates the pipeline of desc into the
  // precompiled set unless the owner or another thread has its key already
  // (present). DrawIndexed takes a ready pipeline instead of creating one and
  // waits for one being created; after a failure (evidence) it creates its own.
  bool PreparePipelines(std::string& evidence);
  bool PrecompilePipeline(const PipelineDesc& desc, bool& present, std::string& evidence);
  // Uploads the levels of a sampled copy (key, layout, version, data)
  // into the open batch, before the draws recorded after it; recreated when
  // its description changes, as in DrawIndexed. A version not above the
  // copy's is skipped (a later upload already ran).
  bool UploadSampled(const DrawTexture& texture, std::string& evidence);
  // Creates or refreshes a vertex or index buffer of the caller's key
  // in the open batch (see BufferUpload), and releases one as
  // ReleaseSampled does: at once when its last use completed, otherwise
  // retired with its fence. An unknown key is fine for the release.
  bool UploadBuffer(const BufferUpload& upload, std::string& evidence);
  bool ReleaseBuffer(uint64_t key, std::string& evidence);
  // Reads back the own RGBA8 texture of a guest header after every submitted
  // operation (front-buffer evidence of D3DDevice_Swap). Waits for the copy.
  // A depth resolve destination gives its R32_FLOAT texels, 4 bytes each.
  bool ReadTexture(uint32_t key, std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height,
                   std::string& evidence);
  // Frame trace (diagnosis): reads back one level (face 0) of a sampled copy
  // after every submitted operation, as its rows (block rows for the BC
  // formats) of row_bytes each. Waits for the copy.
  bool ReadSampled(uint32_t key, uint32_t level, std::vector<uint8_t>& rows, uint32_t& format, uint32_t& width,
                   uint32_t& height, uint32_t& row_bytes, std::string& evidence);
  // Reads back an own single-sample RGBA8 surface whose last use is the
  // operation of after_fence, once that fence has completed (isolated START
  // target). Waits for the copy.
  bool ReadSurface(uint32_t key, uint64_t after_fence, std::vector<uint8_t>& rgba, uint32_t& width,
                   uint32_t& height, std::string& evidence);
  // The failure of a posted operation. From then on every GPU
  // operation is refused with this evidence: nothing is recorded or closed and
  // the open batch is never submitted. WaitIdle still waits for the batches
  // submitted before and then reports false, so no fence reaches the title.
  // Lifecycle (frontend, Shutdown) stays available. Owner thread only.
  void LatchFailure(uint64_t sequence, const std::string& evidence);
  bool Latched() const;
#if defined(NATIVE_RENDERER_ENGINE_FAULT_TEST)
  // Host test: a sampled copy at address (a 4 x 4 RGBA8 resource with an
  // SRV slot), as a draw leaves it, last used by the open batch when in_flight.
  bool SeedSampledForTest(uint32_t address, bool in_flight, std::string& evidence);
#endif
 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace gpu
