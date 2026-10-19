// Optional skip of the startup intros (PC Options > Game > Skip intros).
//
// At boot the game plays Ubisoft.bik, then loads the "CorporateLogo_US/EU" world, which shows the
// Viz Media, TAG Fighting Technology and Dolby logos before moving on to the title screen.

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <chrono>
#include <thread>

#include "generated/default/narutobb_init.h"
#include "title_screen.h"

REXCVAR_DEFINE_BOOL(log_world_loads, false, "Game", "Log every world the game loads (diagnostics)");
REXCVAR_DEFINE_BOOL(skip_intros, false, "Game",
                    "Skip the Ubisoft video and the logo screens when the game starts");

namespace {

extern "C" void __imp__sub_827F7DF0(PPCContext& __restrict, uint8_t*);  // load world by name
extern "C" void __imp__sub_8222D838(PPCContext& __restrict, uint8_t*);  // CreateFile
extern "C" void __imp__sub_821F7F48(PPCContext& __restrict, uint8_t*);  // set last error (NTSTATUS)

constexpr uint32_t kStatusObjectNameNotFound = 0xC0000034;
constexpr uint32_t kInvalidHandle = 0xFFFFFFFF;

bool EndsWithNoCase(const std::string& text, const char* suffix) {
  const size_t n = std::strlen(suffix);
  if (text.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(text[text.size() - n + i])) != suffix[i]) {
      return false;
    }
  }
  return true;
}

std::string ReadGuestString(uint8_t* base, uint32_t addr) {
  std::string out;
  if (!addr) return out;
  for (size_t i = 0; i < 260; ++i) {
    const char c = static_cast<char>(REX_LOAD_U8(addr + i));
    if (!c) break;
    out.push_back(c);
  }
  return out;
}

}  // namespace

REX_HOOK_RAW(sub_827F7DF0) {
  if (REXCVAR_GET(log_world_loads)) {
    // Guest back trace: each frame stores the back chain at 0(r1) and the caller's LR at -8 of
    // the previous frame.
    std::string trace;
    uint32_t frame = ctx.r1.u32;
    for (int i = 0; i < 8 && frame; ++i) {
      const uint32_t next = REX_LOAD_U32(frame);
      // Stop at the top of the thread's stack (64 KB aligned, not mapped beyond).
      if (!next || next <= frame || next - frame > 0x10000 || (next & 0xFFFF) == 0) break;
      trace += fmt::format(" {:08X}", REX_LOAD_U32(next - 8));
      frame = next;
    }
    REXLOG_INFO("[intro] load world '{}' (r3={:08X} r5={} lr={:08X}) trace:{}",
                ReadGuestString(base, ctx.r4.u32), ctx.r3.u32, ctx.r5.u32,
                static_cast<uint32_t>(ctx.lr), trace);
  }
  if (REXCVAR_GET(skip_intros) && ReadGuestString(base, ctx.r4.u32).rfind("CorporateLogo", 0) == 0) {
    REXLOG_INFO("[intro] skipping the startup logos");
    return;
  }
  __imp__sub_827F7DF0(ctx, base);
}

// The Ubisoft video: report the file as missing, like the game's own CreateFile does for a file
// that is not on the disc, so the video player ends at once.
REX_HOOK_RAW(sub_8222D838) {
  const bool intro_video = EndsWithNoCase(ReadGuestString(base, ctx.r3.u32), "ubisoft.bik");
  // With the PC title screen up, the intros wait for it, so they play after it instead of
  // unseen behind it.
  while (intro_video && TitleScreen::IsOpen()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (REXCVAR_GET(skip_intros) && intro_video) {
    REXLOG_INFO("[intro] skipping the Ubisoft video");
    ctx.r3.u64 = kStatusObjectNameNotFound;
    __imp__sub_821F7F48(ctx, base);
    ctx.r3.u64 = kInvalidHandle;
    return;
  }
  __imp__sub_8222D838(ctx, base);
}
