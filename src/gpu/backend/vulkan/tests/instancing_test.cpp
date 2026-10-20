#include "gpu/backend/engine.h"
#include "gpu/backend/pipeline_keys.h"
#include "gpu/backend/tests/test_backend.h"
#include "instancing_vs.h"
#include "instancing_ps.h"
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace gpu;
int main(int argc, char** argv) {
  if (argc != 2 || std::filesystem::exists(argv[1])) return 2;
  std::ofstream output(argv[1]);
  if (!output) return 2;
  bool passed = true;
  auto check = [&](const char* name, bool ok) {
    passed &= ok;
    output << "{\"kind\":\"" << name << "\",\"ok\":" << (ok ? "true" : "false") << "}\n";
  };
  Engine engine;
  std::string evidence;
  if (!engine.Initialize(evidence, TestRenderMode())) {
    output << evidence << '\n';
    return 3;
  }
  SurfaceDesc surface{0x9200, 64, 32};
  check("surface", engine.CreateSurface(surface, evidence));
  DrawRequest draw;
  draw.vs = {"bb-instancing-vs", ShaderStage::kVertex, ShaderFormat::kSpirv,
      std::make_shared<const std::vector<uint8_t>>(reinterpret_cast<const uint8_t*>(kInstanceVs),
          reinterpret_cast<const uint8_t*>(kInstanceVs) + sizeof(kInstanceVs))};
  draw.ps = {"bb-instancing-ps", ShaderStage::kPixel, ShaderFormat::kSpirv,
      std::make_shared<const std::vector<uint8_t>>(reinterpret_cast<const uint8_t*>(kInstancePs),
          reinterpret_cast<const uint8_t*>(kInstancePs) + sizeof(kInstancePs))};
  std::vector<VertexElement> elements{
      {"POSITION", 0, VertexFormat::kR32G32Float, 0, 0},
      {"TEXCOORD", 0, VertexFormat::kR32Float, 1, 0, 1}};
  const auto instanced_id = ElementsId(elements);
  elements[1].instance_step = 0;
  check("distinct-layout-identity", ElementsId(elements) != instanced_id);
  elements[1].instance_step = 1;
  draw.elements = std::make_shared<const std::vector<VertexElement>>(elements);
  const float vertices[]{-1, -1, 0, -1, 0, 1, -1, 1};
  const float offsets[]{0, 1};
  draw.streams = {{std::vector<uint8_t>(sizeof(vertices)), 8},
                  {std::vector<uint8_t>(sizeof(offsets)), 4}};
  std::memcpy(draw.streams[0].data.data(), vertices, sizeof(vertices));
  std::memcpy(draw.streams[1].data.data(), offsets, sizeof(offsets));
  draw.indices = {0, 0, 1, 0, 2, 0, 0, 0, 2, 0, 3, 0};
  draw.index_count = 6;
  draw.instance_count = 2;
  draw.color_key = surface.key;
  draw.viewport = {0, 0, 64, 32, 0, 1};
  draw.scissor = {0, 0, 64, 32};
  draw.vs_constants.resize(4096);
  draw.ps_constants.resize(4096);
  uint64_t fence = 0;
  check("indexed-instances", engine.DrawIndexed(draw, fence, evidence));
  std::vector<uint8_t> pixels;
  uint32_t width = 0, height = 0;
  bool correct = engine.ReadSurface(surface.key, fence, pixels, width, height, evidence) &&
                 width == 64 && height == 32 && pixels.size() == 64 * 32 * 4;
  if (correct) for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
    const std::array<uint8_t, 4> expected = x < 32 ? std::array<uint8_t, 4>{255, 0, 0, 255}
                                                  : std::array<uint8_t, 4>{0, 255, 0, 255};
    correct &= std::memcmp(pixels.data() + (y * width + x) * 4, expected.data(), 4) == 0;
  }
  check("every-instance-pixel", correct);
  check("release", engine.ReleaseRemaining(evidence));
  check("shutdown", engine.Shutdown(evidence));
  return passed ? 0 : 1;
}
