// Optional weather (PC Options > Cheats): one atmosphere for free roam and Story Mode.
//
// The game keeps the current atmosphere (Day, Night, Morning, Day Overcast, Evening, Sunset) as
// loading condition 12 of the world (bits 12..15 of the word at +24 of [[0x833A3464]+76]; the
// game's own debug page "LOADING CONDITIONS", sub_829A4DF0, edits it). Every area picks its sky,
// lights and effects from it when it loads. The game sets it two ways, both through
// sub_827C0980: scripts call sub_82806990(12, value), and loading a save restores all conditions
// with sub_827BD4A0. Both are overridden here, so the next area load uses the chosen weather.

#include <cstdint>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "generated/default/broken_bond_init.h"

REXCVAR_DEFINE_STRING(weather, "game", "Game",
                      "Force one weather everywhere: game, day, night, morning, overcast, evening, sunset");

extern "C" void __imp__sub_82806990(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_827BD4A0(PPCContext& __restrict, uint8_t*);
extern "C" void sub_827C0980(PPCContext& __restrict, uint8_t*);

namespace {
constexpr uint32_t kAtmosphereCondition = 12;
constexpr uint32_t kWorldGlobals = 0x833A3464;  // [kWorldGlobals]+76 = the loading conditions

uint32_t Load32(uint8_t* base, uint32_t address) {
  return _byteswap_ulong(*reinterpret_cast<uint32_t*>(base + address));
}

// The game's own atmosphere numbers, or -1 to leave the game's choice.
int ForcedAtmosphere() {
  const std::string value = REXCVAR_GET(weather);
  static const char* const kNames[] = {"day", "night", "morning", "overcast", "evening", "sunset"};
  for (int i = 0; i < 6; ++i) {
    if (value == kNames[i]) return i;
  }
  return -1;
}
}  // namespace

// sub_82806990(condition, value): the scripts' way to set a loading condition.
REX_HOOK_RAW(sub_82806990) {
  const int forced = ForcedAtmosphere();
  if (ctx.r3.u32 == kAtmosphereCondition && forced >= 0) ctx.r4.u64 = static_cast<uint64_t>(forced);
  __imp__sub_82806990(ctx, base);
}

// sub_827BD4A0 restores every loading condition from a save; the forced atmosphere is set again
// right after it.
REX_HOOK_RAW(sub_827BD4A0) {
  __imp__sub_827BD4A0(ctx, base);
  const int forced = ForcedAtmosphere();
  const uint32_t globals = Load32(base, kWorldGlobals);
  if (forced < 0 || !globals) return;
  const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64;
  ctx.r3.u64 = Load32(base, globals + 76);
  ctx.r4.u64 = kAtmosphereCondition;
  ctx.r5.u64 = static_cast<uint64_t>(forced);
  sub_827C0980(ctx, base);
  ctx.r3.u64 = r3;
  ctx.r4.u64 = r4;
  ctx.r5.u64 = r5;
}
