// Host test of the shader packages embedded in the build (package.h), with
// no guest code and no GPU. The DXIL package holds a module for every shader
// of its index and the variants the masks ask for. The SPIR-V package holds a
// module for every shader and every pass, each decodes from smol-v to a SPIR-V
// module whose SHA-256 is the one of its index line, and the manifest carries
// the contract of the index and the SHA-256 of each pass. Prints one JSON line;
// exits 0 when every check passes.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fmt/format.h>
#include "gpu/common/sha256.h"
#include "gpu/shaders/package.h"

namespace {
struct Line {
  std::string stage, name;
  uint32_t mask = 0;
  std::string spirv_sha256;
};

// The shader lines of an index, the value of its contract line, and its pass
// lines (pass, name, stage, layout, SHA-256) as Line{stage, name, 0, SHA-256}.
std::vector<Line> ReadIndex(const std::string& text, std::string& contract, std::vector<Line>* passes = nullptr) {
  std::vector<Line> lines;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string row = text.substr(at, end - at);
    at = end + 1;
    if (!row.empty() && row.back() == '\r') row.pop_back();
    std::vector<std::string> f;
    for (size_t s = 0; s <= row.size();) {
      size_t t = row.find('\t', s);
      if (t == std::string::npos) t = row.size();
      f.push_back(row.substr(s, t - s));
      s = t + 1;
    }
    if (f.size() == 2 && f[0] == "contract") contract = f[1];
    if (passes && f.size() == 5 && f[0] == "pass") passes->push_back({f[2], f[1], 0, f[4]});
    if (f.size() >= 6 && (f[0] == "vs" || f[0] == "ps"))
      lines.push_back({f[0], f[1], uint32_t(std::stoul(f[5])), f.size() > 6 ? f[6] : std::string()});
  }
  return lines;
}

double Ms(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}
}  // namespace

int main() {
  std::vector<std::string> failures;
  auto fail = [&](std::string what) {
    if (failures.size() < 16) failures.push_back(std::move(what));
  };

  auto started = std::chrono::steady_clock::now();
  gpu::ShaderPackage dxil;
  std::string error, contract;
  if (!gpu::UnpackShaderPackage(gpu::ShaderFormat::kDxil, dxil, error)) fail("dxil-unpack-" + error);
  const double dxil_unpack_ms = Ms(started);
  const auto dxil_lines = ReadIndex(dxil.index, contract);
  size_t variants_expected = 0;
  for (const auto& line : dxil_lines) {
    if (!dxil.modules.count(line.name)) fail("dxil-module-missing-" + line.name);
    for (uint32_t value : {2u, 8u})  // The values the draw links (build-shader-cache.py).
      if (value & line.mask) {
        ++variants_expected;
        if (!dxil.variants.count(fmt::format("{}+spec{}", line.name, value))) fail("dxil-variant-missing-" + line.name);
      }
  }
  if (dxil_lines.empty() || dxil.modules.size() != dxil_lines.size()) fail("dxil-index-modules");
  if (dxil.variants.size() != variants_expected) fail("dxil-variants");

  started = std::chrono::steady_clock::now();
  gpu::ShaderPackage spirv;
  contract.clear();
  const bool embedded = gpu::UnpackShaderPackage(gpu::ShaderFormat::kSpirv, spirv, error);
  if (!embedded) fail("spirv-unpack-" + error);
  const double spirv_unpack_ms = Ms(started);
  std::vector<Line> pass_lines;
  const auto spirv_lines = ReadIndex(spirv.index, contract, &pass_lines);
  // Decodes the module of each line from the records and checks its SHA-256.
  auto decode = [&](const std::vector<Line>& lines, const auto& records, const char* what, size_t& bytes) {
    size_t decoded = 0;
    for (const auto& line : lines) {
      auto stored = records.find(line.name);
      if (stored == records.end()) {
        fail(fmt::format("{}-missing-{}", what, line.name));
        continue;
      }
      const std::vector<uint8_t> module = gpu::DecodeShaderModule(spirv, stored->second);
      uint32_t magic = 0;
      if (module.size() >= 4) std::memcpy(&magic, module.data(), 4);
      if (module.empty() || magic != 0x07230203u) fail(fmt::format("{}-decode-{}", what, line.name));
      else if (gpu::Sha256Hex(module.data(), module.size()) != line.spirv_sha256)
        fail(fmt::format("{}-sha256-{}", what, line.name));
      else ++decoded;
      bytes += module.size();
    }
    return decoded;
  };
  started = std::chrono::steady_clock::now();
  size_t decoded_bytes = 0, pass_bytes = 0;
  const size_t decoded = decode(spirv_lines, spirv.modules, "spirv-module", decoded_bytes);
  const double decode_ms = Ms(started);
  const size_t passes_decoded = decode(pass_lines, spirv.passes, "spirv-pass", pass_bytes);
  if (embedded && (spirv_lines.size() != dxil_lines.size() || spirv.modules.size() != spirv_lines.size()))
    fail("spirv-index-modules");
  if (embedded && (contract.size() != 64 ||
                   spirv.manifest.find(fmt::format("\"contract_sha256\":\"{}\"", contract)) == std::string::npos))
    fail("spirv-manifest-contract");
  if (embedded && (pass_lines.empty() || spirv.passes.size() != pass_lines.size())) fail("spirv-index-passes");
  for (const auto& line : pass_lines)  // The manifest (canonical JSON) lists each pass with its module.
    if (spirv.manifest.find(fmt::format("\"{}\":\"{}\"", line.name, line.spirv_sha256)) == std::string::npos)
      fail("spirv-manifest-pass-" + line.name);

  std::string listed;
  for (const auto& f : failures) listed += fmt::format("{}\"{}\"", listed.empty() ? "" : ",", f);
  std::printf("%s\n", fmt::format(
      "{{\"verdict\":\"{}\",\"dxil\":{{\"shaders\":{},\"variants\":{},\"payload_sha256\":\"{}\",\"unpack_ms\":{:.1f}}},"
      "\"spirv\":{{\"shaders\":{},\"decoded\":{},\"decoded_bytes\":{},\"payload_sha256\":\"{}\",\"contract\":\"{}\","
      "\"manifest_bytes\":{},\"unpack_ms\":{:.1f},\"decode_ms\":{:.1f},"
      "\"passes\":{{\"count\":{},\"decoded\":{},\"decoded_bytes\":{}}}}},\"failures\":[{}]}}",
      failures.empty() ? "PASS" : "FAIL", dxil_lines.size(), dxil.variants.size(), dxil.payload_sha256, dxil_unpack_ms,
      spirv_lines.size(), decoded, decoded_bytes, spirv.payload_sha256, contract, spirv.manifest.size(), spirv_unpack_ms,
      decode_ms, pass_lines.size(), passes_decoded, pass_bytes, listed).c_str());
  return failures.empty() ? 0 : 1;
}
