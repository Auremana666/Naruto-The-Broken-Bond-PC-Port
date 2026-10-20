#pragma once

// The switch of the diagnostic instruments of the D3D boundary: the START
// evidence, the guard census, the traces and captures, the measurement and its
// profile, the frame state, the swapchain proof, the close tests and the
// per-operation evidence records. On with REX_NATIVE_DIAGNOSTICS=1; off by
// default.
// Off, the renderer runs without them: no stop at the START occurrence, no
// front-buffer readbacks, no texture verification, no per-operation records nor
// synchronous evidence operations, and each instrument's own variable is
// ignored. Lifecycle records, guards, failures, stops, the summaries at a stop
// and the close stay. Inline: the engine's profile uses it in the host tests too.
#include <cstdlib>
#include <string_view>

namespace gpu {
inline bool Diagnostics() {
  static const bool value = [] {
    const char* text = std::getenv("REX_NATIVE_DIAGNOSTICS");
    return text && std::string_view(text) == "1";
  }();
  return value;
}
}  // namespace gpu
