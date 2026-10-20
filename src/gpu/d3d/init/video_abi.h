#pragma once
#include "gpu/d3d/init/initializer_abi.h"

namespace gpu {
inline InitializerAbiImage VideoReturnAbi(const InitializerAbiImage& entry) {
  PPCContext expected;
  std::memcpy(&expected, entry.data(), entry.size());
  expected.r3.u64 = 1;
  return CaptureInitializerAbi(expected);
}
inline InitializerAbiImage VideoNaturalCallerAbi(const InitializerAbiImage& entry) {
  PPCContext expected;
  std::memcpy(&expected, entry.data(), entry.size());
  expected.r3.u64 = 1;
  expected.cr0.compare<int32_t>(expected.r3.s32, 0, expected.xer);
  expected.r3.u64 = expected.r31.u64;
  expected.lr = 0x821B41BC;
  return CaptureInitializerAbi(expected);
}
} // namespace gpu
