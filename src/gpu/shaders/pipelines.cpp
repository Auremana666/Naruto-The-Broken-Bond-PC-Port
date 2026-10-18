// Precompiled pipelines. The list, shaders/pipeline_cache.inc (optional), is
// generated from the pipelines.json of earlier boots and git-ignored like the
// shader cache: the shader names, the layout and the
// state words of each key. At the first draw (shader cache loaded) the engine
// prepares its root signature and a pool of the dispatcher compiles the list with
// the DXIL of the cache and the spec constant variants the draw takes. A draw takes
// a ready pipeline, or waits for one being created; one missing from the list is
// created by the draw. REX_NATIVE_PSO_PRECOMPILE=0 turns the pool off.
#include "gpu/shaders/pipelines.h"
#include "gpu/d3d/draw.h"
#include "gpu/shaders/shaders.h"
#include "gpu/d3d/frame.h"
#include "gpu/d3d/d3d.h"
#include "gpu/backend/dispatcher.h"
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
struct PipelineCacheElement {
  const char* semantic;  // Static string (XenosRecomp semantics), as VertexElement.
  uint32_t semantic_index, format, slot, offset;
  uint32_t instance_step = 0;  // As VertexElement; written only when not 0.
};
struct PipelineCacheEntry {
  const char* vs;
  const char* ps;  // With "+specN", or "depth-only".
  uint64_t key, layout;
  uint32_t first_element, element_count;
  std::array<uint32_t, 21> state;
};
#if __has_include("gpu/shaders/pipeline_cache.inc")
#include "gpu/shaders/pipeline_cache.inc"
constexpr std::span<const PipelineCacheEntry> kPipelineCache{kPipelineCacheEntries};
constexpr std::span<const PipelineCacheElement> kPipelineElements{kPipelineCacheElements};
#else
constexpr std::span<const PipelineCacheEntry> kPipelineCache{};
constexpr std::span<const PipelineCacheElement> kPipelineElements{};
constexpr const char* kPipelineCacheIdentity = "";
#endif

struct PipelinePrecompile {
  std::once_flag once;
  std::chrono::steady_clock::time_point started;
  unsigned threads = 0;
  std::vector<VertexElements> layouts;  // Per entry, shared by layout id.
  std::atomic<uint64_t> created{0}, present{0}, failed{0}, unresolved{0}, mismatched{0};
  // Entries taken as the alpha to coverage variant (transparency antialiasing
  // on), and alpha to coverage entries taken back as the alpha test (off).
  std::atomic<uint64_t> alpha_to_coverage{0}, alpha_test{0};
  std::mutex mutex;
  std::string first_failure;  // JSON value.
} pipeline_precompile;

void PrecompileNote(std::atomic<uint64_t>& counter, std::string failure) {
  ++counter;
  std::lock_guard lock(pipeline_precompile.mutex);
  if (pipeline_precompile.first_failure.empty()) pipeline_precompile.first_failure = std::move(failure);
}

// One entry of the list on a pool thread: its shaders by name, the key checked
// against the engine's, then the engine's PrecompilePipeline.
void PrecompileEntry(size_t i, Engine& engine) {
  const PipelineCacheEntry& entry = kPipelineCache[i];
  auto unresolved = [&](const char* why) {
    PrecompileNote(pipeline_precompile.unresolved, fmt::format("{{\"reason\":\"{}\",\"vs\":\"{}\",\"ps\":\"{}\"}}",
                                                               why, entry.vs, entry.ps));
  };
  auto vs = shader_cache.by_name.find(entry.vs);
  if (vs == shader_cache.by_name.end()) return unresolved("vs-not-in-cache");
  PipelineDesc desc;
  desc.vs_key = entry.vs;
  desc.ps_key = entry.ps;
  desc.vs_dxil = LoadDxil(*vs->second);
  if (!desc.vs_dxil) return unresolved("vs-dxil-unreadable");
  std::string variant_key;  // The pixel shader key the draw asks for now, when the list holds the other.
  uint32_t linked = 0;
  if (desc.ps_key != "depth-only") {
    std::string name = desc.ps_key;
    uint32_t spec = 0;
    if (const size_t plus = name.find("+spec"); plus != std::string::npos) {
      spec = uint32_t(std::stoul(name.substr(plus + 5)));
      name.resize(plus);
    }
    auto ps = shader_cache.by_name.find(name);
    if (ps == shader_cache.by_name.end()) return unresolved("ps-not-in-cache");
    // The list holds the variants of the boots it came from, with the
    // transparency antialiasing those boots had. The draw asks for the alpha
    // test (only TransparencySpec turns it into alpha to coverage), so an
    // entry goes back to the alpha test and through TransparencySpec as the
    // draw does: alpha to coverage in a multisampled target when the option
    // is on, the alpha test otherwise.
    const uint32_t requested = spec == kSpecAlphaToCoverage ? kSpecAlphaTest : spec;
    linked = TransparencySpec(requested, ps->second->spec_mask, entry.state[8], entry.state[0]);
    if (linked != spec) variant_key = fmt::format("{}+spec{}", name, linked);
    std::string error;
    desc.ps_dxil = LoadShader(*ps->second, linked, error);
    if (!desc.ps_dxil) return unresolved("ps-dxil-unavailable");
  }
  desc.elements = pipeline_precompile.layouts[i];
  desc.layout_id = entry.layout;
  desc.state = entry.state;
  if (Engine::PipelineKey(desc.vs_key, desc.ps_key, desc.layout_id, desc.state) != entry.key)
    return PrecompileNote(pipeline_precompile.mismatched, fmt::format("{{\"reason\":\"key\",\"key\":\"{:016x}\"}}",
                                                                      entry.key));
  // As the draw: the variant's key; alpha to coverage turns state word 11 on,
  // and the alpha test keeps the word of the entry (the title's alpha to mask).
  if (!variant_key.empty()) {
    desc.ps_key = variant_key;
    if (linked == kSpecAlphaToCoverage) desc.state[11] = 1;
  }
  bool present = false;
  std::string evidence;
  if (!engine.PrecompilePipeline(desc, present, evidence))
    return PrecompileNote(pipeline_precompile.failed, evidence);
  ++(present ? pipeline_precompile.present : pipeline_precompile.created);
  if (!variant_key.empty()) ++(linked == kSpecAlphaToCoverage ? pipeline_precompile.alpha_to_coverage
                                                               : pipeline_precompile.alpha_test);
}

void StartPipelinePrecompile() {
  auto& p = pipeline_precompile;
  const char* off = std::getenv("REX_NATIVE_PSO_PRECOMPILE");
  const char* skip = off && std::string_view(off) == "0" ? "disabled" : kPipelineCache.empty() ? "list-absent"
                     : !shader_cache.loaded ? "shader-cache-unavailable" : nullptr;
  auto* dispatcher = host::ActiveDispatcher();
  std::string prepared;
  if (!skip && (!dispatcher || !dispatcher->Run([](Engine& engine, std::string& out) {
        return engine.PreparePipelines(out);
      }, prepared))) skip = "prepare-failed";
  if (skip) {
    host::Record("native-d3d-pipeline-precompile-start", fmt::format(
        "\"started\":false,\"reason\":\"{}\",\"list\":{},\"identity\":\"{}\",\"prepare\":{}", skip,
        kPipelineCache.size(), kPipelineCacheIdentity, prepared.empty() ? "null" : prepared));
    return;
  }
  std::unordered_map<uint64_t, VertexElements> shared;
  for (const auto& entry : kPipelineCache) {
    auto& layout = shared[entry.layout];
    if (!layout) {
      std::vector<VertexElement> elements;
      for (uint32_t e = entry.first_element; e < entry.first_element + entry.element_count; ++e) {
        const auto& c = kPipelineElements[e];
        elements.push_back({c.semantic, c.semantic_index, c.format, c.slot, c.offset, c.instance_step});
      }
      layout = std::make_shared<const std::vector<VertexElement>>(std::move(elements));
    }
    p.layouts.push_back(layout);
  }
  // Two thirds of the hardware threads, at least two.
  p.threads = std::max(2u, std::thread::hardware_concurrency() * 2 / 3);
  p.started = std::chrono::steady_clock::now();
  const bool started = dispatcher->StartPool(kPipelineCache.size(), p.threads, PrecompileEntry, [](bool stopped) {
    auto& p = pipeline_precompile;
    std::string failure;
    {
      std::lock_guard lock(p.mutex);
      failure = p.first_failure.empty() ? "null" : p.first_failure;
    }
    host::Record("native-d3d-pipeline-precompile", fmt::format(
        "\"list\":{},\"threads\":{},\"created\":{},\"present\":{},\"failed\":{},\"unresolved\":{},\"mismatched\":{},"
        "\"alpha_to_coverage\":{},\"alpha_test\":{},\"stopped\":{},\"ms\":{},\"swap\":{},\"first_failure\":{}",
        kPipelineCache.size(),
        p.threads, p.created.load(), p.present.load(), p.failed.load(), p.unresolved.load(), p.mismatched.load(),
        p.alpha_to_coverage.load(), p.alpha_test.load(), stopped,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - p.started).count(),
        swaps.load(), failure));
  });
  host::Record("native-d3d-pipeline-precompile-start", fmt::format(
      "\"started\":{},\"list\":{},\"layouts\":{},\"threads\":{},\"identity\":\"{}\",\"swap\":{}", started,
      kPipelineCache.size(), shared.size(), p.threads, kPipelineCacheIdentity, swaps.load()));
}

void StartPipelinePrecompileOnce() { std::call_once(pipeline_precompile.once, StartPipelinePrecompile); }
}  // namespace gpu

namespace gpu {
// The pipelines created with their stable identities, also as
// pipelines.json in REX_NATIVE_FRAME_DIR (the input of the precompiled list),
// on the stop and on the close.
void RecordPipelines() {
  auto* dispatcher = host::ActiveDispatcher();
  std::string pipelines;
  if (!dispatcher || !dispatcher->Run([](Engine& engine, std::string& o) { o = engine.PipelineList(); return true; },
                                      pipelines) || pipelines.empty()) return;
  size_t count = 0;
  for (size_t at = pipelines.find("{\"key\""); at != std::string::npos; at = pipelines.find("{\"key\"", at + 1)) ++count;
  std::string file = "null";
  if (const char* dir = std::getenv("REX_NATIVE_FRAME_DIR"); dir && *dir) {
    std::string path = std::string(dir) + "/pipelines.json";
    std::replace(path.begin(), path.end(), '\\', '/');  // A JSON-safe path in the record.
    if (std::FILE* out = std::fopen(path.c_str(), "wb")) {
      std::fwrite(pipelines.data(), 1, pipelines.size(), out);
      std::fclose(out);
      file = "\"" + path + "\"";
    }
  }
  host::Record("native-d3d-pipelines", fmt::format("\"count\":{},\"file\":{},\"pipelines\":{}", count, file, pipelines));
}
}  // namespace gpu
