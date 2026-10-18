#pragma once

#include <rex/ppc/context.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace gpu {
static_assert(std::is_trivially_copyable_v<PPCContext>);
using InitializerAbiImage = std::array<uint8_t, sizeof(PPCContext)>;
// Copy the complete representation, including padding. Memberwise copy
// assignment does not promise that padding survives for a bytewise ABI proof.
inline InitializerAbiImage CaptureInitializerAbi(const PPCContext& context) {
  InitializerAbiImage image{};
  std::memcpy(image.data(), &context, image.size());
  return image;
}
inline InitializerAbiImage InitializerNaturalCallerAbi(const InitializerAbiImage& entry) {
  auto expected = entry;
  const uint64_t lr = 0x821B41AC;
  std::memcpy(expected.data() + offsetof(PPCContext, lr), &lr, sizeof(lr));
  std::memcpy(expected.data() + offsetof(PPCContext, r3),
              entry.data() + offsetof(PPCContext, r31), sizeof(PPCRegister));
  std::memcpy(expected.data() + offsetof(PPCContext, r4),
              entry.data() + offsetof(PPCContext, r30), sizeof(PPCRegister));
  return expected;
}
inline bool InitializerAbiMatches(const InitializerAbiImage& image, const PPCContext& context) {
  return std::memcmp(image.data(), &context, image.size()) == 0;
}
}  // namespace gpu
