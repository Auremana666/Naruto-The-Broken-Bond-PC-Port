#pragma once
#include "gpu/backend/contracts.h"
#include <algorithm>
namespace gpu {
enum class OutputPass : uint8_t { kBilinear, kCasSharpen, kCasResample, kFsrEasu, kFsrRcas };
struct OutputStep {
  OutputPass pass;
  uint32_t width, height;
};
inline const char* OutputPassName(OutputPass pass) {
  switch (pass) {
    case OutputPass::kCasSharpen: return "cas-sharpen";
    case OutputPass::kCasResample: return "cas-resample";
    case OutputPass::kFsrEasu: return "fsr-easu";
    case OutputPass::kFsrRcas: return "fsr-rcas";
    default: return "bilinear";
  }
}
inline std::vector<OutputStep> OutputPlan(const PresentMode& mode, uint32_t in_width, uint32_t in_height,
                                   uint32_t out_width, uint32_t out_height) {
  std::vector<OutputStep> plan;
  uint32_t w = in_width, h = in_height;
  if (mode.effect == PresentEffect::kFsr && (w < out_width || h < out_height)) {
    for (uint32_t n = 0; n < mode.fsr_passes && (w < out_width || h < out_height); ++n) {
      w = std::min(w * 2, out_width);
      h = std::min(h * 2, out_height);
      plan.push_back({OutputPass::kFsrEasu, w, h});
    }
    plan.push_back({OutputPass::kFsrRcas, w, h});
  } else if (mode.effect != PresentEffect::kBilinear) {
    const uint32_t pre_w = w, pre_h = h;
    w = std::min(w * 2, out_width);
    h = std::min(h * 2, out_height);
    plan.push_back({w == pre_w && h == pre_h ? OutputPass::kCasSharpen : OutputPass::kCasResample, w, h});
  }
  if (plan.empty() || plan.back().width != out_width || plan.back().height != out_height)
    plan.push_back({OutputPass::kBilinear, out_width, out_height});
  return plan;
}
}
