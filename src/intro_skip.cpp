// Optional boot logo skip for the supported Broken Bond executable.
// See docs/launcher-game-features.md for the REA/IDA evidence and limits.

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/broken_bond_init.h"

REXCVAR_DEFINE_BOOL(skip_intro_videos, false, "broken_bond",
                    "Skip the Ubisoft boot logo video; preserve story and attract videos");

namespace {

constexpr uint32_t kSkipStatus = 0xC0000035;
constexpr uint32_t kSkipError = 80;
constexpr uint32_t kMaximumPathLength = 260;

std::string ReadGuestPath(uint8_t* base, uint32_t address) {
  if (!address ||
      address > std::numeric_limits<uint32_t>::max() - kMaximumPathLength) {
    return {};
  }

  std::string path;
  for (uint32_t offset = 0; offset < kMaximumPathLength; ++offset) {
    char character = static_cast<char>(REX_LOAD_U8(address + offset));
    if (!character) {
      return path;
    }
    if (character >= 'A' && character <= 'Z') {
      character += 'a' - 'A';
    }
    path.push_back(character);
  }
  // A truncated path must never identify a video to skip.
  return {};
}

bool IsBootLogo(std::string_view path) {
  const auto separator = path.find_last_of("\\/");
  const auto name = separator == std::string_view::npos
                        ? path
                        : path.substr(separator + 1);
  return name == "ubisoft.bik";
}

}  // namespace

REX_HOOK_RAW(sub_8222D838) {
  // This wrapper also handles writable files. Only intercept read-only opens.
  const bool read_only = (ctx.r4.u32 & 0x80000000) != 0 &&
                         (ctx.r4.u32 & 0x40000000) == 0;
  if (REXCVAR_GET(skip_intro_videos) && read_only) {
    const std::string path = ReadGuestPath(base, ctx.r3.u32);
    if (IsBootLogo(path)) {
      REXLOG_INFO("[broken_bond] boot logo video skipped: {}", path);
      // Mirror the wrapper's existing special failure path, including the
      // thread-local last-error side effects used by its file/video callers.
      ctx.r3.s64 = static_cast<int32_t>(kSkipStatus);
      __imp__sub_821F7F48(ctx, base);
      ctx.r3.u64 = kSkipError;
      __imp__sub_821F7F28(ctx, base);
      ctx.r3.s64 = -1;
      return;
    }
  }
  __imp__sub_8222D838(ctx, base);
}
