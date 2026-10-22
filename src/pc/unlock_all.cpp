// Optional unlocks (PC Options > Cheats): every Fight Mode character and every arena.
//
// The game keeps unlocks in its persistent flag store; the character select asks it with
// sub_82BCA3F0 (flag id = character id) and the arena select with the same function (flag id =
// arena id * 100). The answer is overridden only for those two callers, so the save itself is
// never changed and turning the option off restores the player's own progress.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "generated/default/broken_bond_init.h"

REXCVAR_DEFINE_BOOL(unlock_all_characters, false, "Game",
                    "Make every character selectable in Fight Mode");
REXCVAR_DEFINE_BOOL(unlock_all_arenas, false, "Game", "Make every arena selectable in Fight Mode");

extern "C" void __imp__sub_82BCA3F0(PPCContext& __restrict, uint8_t*);

namespace {
constexpr uint32_t kCharacterSelectCaller = 0x82918474;
constexpr uint32_t kArenaSelectCaller = 0x82916D9C;
}  // namespace

REX_HOOK_RAW(sub_82BCA3F0) {
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  if ((caller == kCharacterSelectCaller && REXCVAR_GET(unlock_all_characters)) ||
      (caller == kArenaSelectCaller && REXCVAR_GET(unlock_all_arenas))) {
    ctx.r3.u64 = 1;  // unlocked
    return;
  }
  __imp__sub_82BCA3F0(ctx, base);
}

// The arena select also locks an arena whose state value (sub_82BCA190) is 2, even when its flag
// is set, so that caller gets a state other than 2.
extern "C" void __imp__sub_82BCA190(PPCContext& __restrict, uint8_t*);

namespace {
constexpr uint32_t kArenaStateCaller = 0x82916DB8;
}  // namespace

REX_HOOK_RAW(sub_82BCA190) {
  if (static_cast<uint32_t>(ctx.lr) == kArenaStateCaller && REXCVAR_GET(unlock_all_arenas)) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82BCA190(ctx, base);
}
