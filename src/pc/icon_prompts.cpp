// Keyboard keys on the game's button icons (work in progress: diagnostics).
//
// Every Flash movie of the interface imports its button icons from _ExternalGenericIcons.swf,
// which Scaleform asks the game's file opener for. sub_822D8778 is Scaleform's helper that calls
// the opener (opener->OpenFile(url)) and returns the file.

#include <cstdint>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/broken_bond_init.h"

REXCVAR_DEFINE_BOOL(log_movie_opens, false, "Controls",
                    "Log every interface movie the game opens (diagnostics)");

extern "C" void __imp__sub_822D8778(PPCContext& __restrict, uint8_t*);

namespace {

uint32_t Load32(uint8_t* base, uint32_t address) {
  uint32_t raw;
  std::memcpy(&raw, base + address, sizeof(raw));
  return __builtin_bswap32(raw);
}

std::string ReadString(uint8_t* base, uint32_t address) {
  std::string out;
  if (address < 0x40000000u) return out;
  for (uint32_t i = 0; i < 260; ++i) {
    const char c = static_cast<char>(base[address + i]);
    if (!c) break;
    out.push_back(c);
  }
  return out;
}

}  // namespace

REX_HOOK_RAW(sub_822D8778) {
  const uint32_t opener = ctx.r3.u32;
  const std::string url = ReadString(base, ctx.r4.u32);
  __imp__sub_822D8778(ctx, base);
  if (!REXCVAR_GET(log_movie_opens)) return;
  const uint32_t file = ctx.r3.u32;
  const uint32_t opener_vt = opener ? Load32(base, opener) : 0;
  std::string fields;
  if (file) {
    for (uint32_t i = 0; i < 12; ++i) fields += fmt::format(" {:08X}", Load32(base, file + i * 4));
  }
  REXLOG_INFO("[movies] open '{}' opener={:08X} vt={:08X} OpenFile={:08X} -> file={:08X}:{}", url,
              opener, opener_vt, opener_vt ? Load32(base, opener_vt + 4) : 0, file, fields);
}
