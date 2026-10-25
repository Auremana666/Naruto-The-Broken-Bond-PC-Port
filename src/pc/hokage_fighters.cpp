// Optional Hokage fighters (PC Options > Cheats): the First and the Second Hokage, who only fight
// in Story Mode, take the Fight Mode slot of Young Itachi or Black Sasuke.
//
// Both Hokages are complete fighters in naruto.bf (Ninja_First.wow, Ninja_Second.wow: model,
// moves, voice), but Fight Mode has no entry for them. A slot is taken over in three steps:
//  - Its world lists (WOLInfo.bin, read once at boot by sub_82BC7230 into a sorted key -> list
//    map at +412 of its object) load the Hokage's fighter world instead of the slot's own one.
//    Everything else the slot loads stays: intro scenes, sounds, the versus artwork.
//  - The Hokage's fighter points at the slot's Fight Mode identity instead of his story one
//    (the identity holds the character id, the name and the fight setup). The game's LZO
//    decompressor, sub_82BBB400(src, src_len, dst, &dst_len), unpacks every bigfile chunk;
//    right after it, the identity key next to the Hokage's sound key is replaced. Data in the
//    bigfile is little endian.
//  - The slot's identity (All_NinjaIdentities.wow) gets the Hokage's name text, so the fight
//    shows FIRST HOKAGE or SECOND HOKAGE. That file is unpacked once, at boot.
// A fighter choice applies at the next fight, the name after a restart. With a Hokage in a
// slot, his own Story Mode fight against Sarutobi uses that slot's identity too, so the option
// is meant for Fight Mode.
//
// For testing, REX_DATA_PATCH=<file> applies extra edits to every unpacked chunk, one per line:
// "<find hex> <replace hex>" (same length).

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/broken_bond_init.h"

REXCVAR_DEFINE_STRING(young_itachi_slot, "itachi", "Game",
                      "Fighter in Young Itachi's Fight Mode slot: itachi, first, second");
REXCVAR_DEFINE_STRING(black_sasuke_slot, "sasuke", "Game",
                      "Fighter in Black Sasuke's Fight Mode slot: sasuke, first, second");

extern "C" void __imp__sub_82BBB400(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82BC7230(PPCContext& __restrict, uint8_t*);

namespace {
struct Hokage {
  const char* value;
  uint32_t fighter_world;  // Ninja_<name>.wow
  uint32_t identity;       // NinjaIdentity_<name>.gao, his story identity
  uint32_t sound;          // NinjaSound_<name>.gao, stored right after the identity key
  uint32_t name_text;      // text id of his name
};

constexpr std::array<Hokage, 2> kHokages = {{
    {"first", 0x2D002A99, 0x2D002AA4, 0x2D002AA7, 0x5E50},
    {"second", 0x2D002AAF, 0x2D002ABB, 0x2D002ABE, 0x5E51},
}};

struct Slot {
  const char* cvar;
  uint32_t fighter_world;           // the slot's own <name>.wow
  uint32_t identity;                // the slot's NinjaIdentity_<name>.gao
  std::array<uint32_t, 2> lists;    // <name>.wol and <name>_Versus.wol
  // In the identity's variables, the character id and name text follow these 8 bytes.
  uint32_t name_lead, id, name_text;
  std::array<uint32_t, 2> list_at;  // guest address of each list record {count, keys, name}
};

std::array<Slot, 2> g_slots = {{
    {"young_itachi_slot", 0x2D00363B, 0x2D003646, {0x2D00363C, 0x4000405D}, 0xFFFFFFFF, 0x63, 0x632F, {}},
    {"black_sasuke_slot", 0x2D0032F2, 0x2D0032FD, {0x2D0032F3, 0x40003F11}, 0x00000000, 0x62, 0x6360, {}},
}};

uint8_t* g_base = nullptr;
std::mutex g_mutex;

uint32_t Load32(uint8_t* base, uint32_t address) {
  return _byteswap_ulong(*reinterpret_cast<uint32_t*>(base + address));
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  *reinterpret_cast<uint32_t*>(base + address) = _byteswap_ulong(value);
}

// The Hokage chosen for a slot, or nullptr for the slot's own fighter.
const Hokage* Chosen(const Slot& slot) {
  const std::string value = rex::cvar::GetFlagByName(slot.cvar);
  for (const Hokage& hokage : kHokages) {
    if (value == hokage.value) return &hokage;
  }
  return nullptr;
}

// Points each slot's world lists at the chosen fighter world.
void ApplyLists() {
  std::lock_guard lock(g_mutex);
  if (!g_base) return;
  for (const Slot& slot : g_slots) {
    const Hokage* hokage = Chosen(slot);
    const uint32_t wanted = hokage ? hokage->fighter_world : slot.fighter_world;
    for (uint32_t record : slot.list_at) {
      if (!record) continue;
      const uint32_t count = Load32(g_base, record), keys = Load32(g_base, record + 4);
      for (uint32_t i = 0; i < count && i < 4096; ++i) {
        const uint32_t key = Load32(g_base, keys + 4 * i);
        bool fighter = key == slot.fighter_world;
        for (const Hokage& h : kHokages) fighter = fighter || key == h.fighter_world;
        if (fighter && key != wanted) Store32(g_base, keys + 4 * i, wanted);
      }
    }
  }
}

struct Patch {
  std::vector<uint8_t> find, replace;
};

std::vector<uint8_t> FromHex(const std::string& text) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    out.push_back(static_cast<uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
  }
  return out;
}

const std::vector<Patch>& TestPatches() {
  static std::vector<Patch> patches;
  static std::once_flag once;
  std::call_once(once, [] {
    const char* path = std::getenv("REX_DATA_PATCH");
    if (!path) return;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
      std::istringstream fields(line);
      std::string find, replace;
      if (!(fields >> find >> replace) || find[0] == '#') continue;
      Patch patch{FromHex(find), FromHex(replace)};
      if (patch.find.empty() || patch.find.size() != patch.replace.size()) continue;
      patches.push_back(std::move(patch));
    }
    REXLOG_INFO("[hokage] {} test data patches from {}", patches.size(), path);
  });
  return patches;
}

void Replace(uint8_t* data, uint32_t length, const uint8_t* find, const uint8_t* replace, size_t n,
             uint32_t dst) {
  for (uint32_t i = 0; i + n <= length; ++i) {
    if (data[i] != find[0] || std::memcmp(data + i, find, n) != 0) continue;
    std::memcpy(data + i, replace, n);
    REXLOG_INFO("[hokage] patched {} bytes at {:08X}+{:X}", n, dst, i);
    i += static_cast<uint32_t>(n) - 1;
  }
}

void PutLE(uint8_t* out, uint32_t value) { std::memcpy(out, &value, 4); }
}  // namespace

// sub_82BC7230 reads WOLInfo.bin; its sorted (key, record) array is at [object+412], [object+416]
// entries. The slots' list records are remembered there, then set for the current choice.
REX_HOOK_RAW(sub_82BC7230) {
  const uint32_t object = ctx.r3.u32;
  __imp__sub_82BC7230(ctx, base);
  const uint32_t entries = Load32(base, object + 412), count = Load32(base, object + 416);
  {
    std::lock_guard lock(g_mutex);
    g_base = base;
    for (uint32_t i = 0; entries && i < count; ++i) {
      const uint32_t key = Load32(base, entries + 8 * i), record = Load32(base, entries + 8 * i + 4);
      for (Slot& slot : g_slots) {
        for (size_t j = 0; j < slot.lists.size(); ++j) {
          if (key == slot.lists[j]) slot.list_at[j] = record;
        }
      }
    }
    int found = 0;
    for (const Slot& slot : g_slots) {
      for (uint32_t record : slot.list_at) found += record != 0;
    }
    REXLOG_INFO("[hokage] {} of {} slot world lists found", found, 2 * g_slots.size());
  }
  static std::once_flag callbacks;
  std::call_once(callbacks, [] {
    for (const Slot& slot : g_slots) {
      rex::cvar::RegisterChangeCallback(slot.cvar, [](std::string_view, std::string_view) { ApplyLists(); });
    }
  });
  ApplyLists();
}

REX_HOOK_RAW(sub_82BBB400) {
  const uint32_t dst = ctx.r5.u32, dst_len = ctx.r6.u32;
  __imp__sub_82BBB400(ctx, base);
  const uint32_t length = Load32(base, dst_len);
  uint8_t* data = base + dst;
  // The Hokage's fighter keeps his own voice (sound key) and takes the slot's identity.
  std::array<bool, kHokages.size()> done{};
  for (const Slot& slot : g_slots) {
    const Hokage* hokage = Chosen(slot);
    if (!hokage) continue;
    const size_t h = static_cast<size_t>(hokage - kHokages.data());
    if (done[h]) continue;  // one identity per Hokage: the first slot that picked him
    done[h] = true;
    uint8_t find[12], replace[12];
    PutLE(find, hokage->identity);
    PutLE(find + 4, hokage->sound);
    PutLE(find + 8, 0x70005A80);
    std::memcpy(replace, find, sizeof(find));
    PutLE(replace, slot.identity);
    Replace(data, length, find, replace, sizeof(find), dst);
  }
  for (const Slot& slot : g_slots) {
    const Hokage* hokage = Chosen(slot);
    if (!hokage) continue;
    uint8_t find[16], replace[16];
    PutLE(find, slot.name_lead);
    PutLE(find + 4, slot.name_lead);
    PutLE(find + 8, slot.id);
    PutLE(find + 12, slot.name_text);
    std::memcpy(replace, find, sizeof(find));
    PutLE(replace + 12, hokage->name_text);
    Replace(data, length, find, replace, sizeof(find), dst);
  }
  for (const Patch& patch : TestPatches()) {
    Replace(data, length, patch.find.data(), patch.replace.data(), patch.find.size(), dst);
  }
}
