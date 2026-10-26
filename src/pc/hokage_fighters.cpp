// Optional Hokage fighters (PC Options > Cheats > Hidden Characters & Skins): the First and the
// Second Hokage, who only fight in Story Mode, take the Fight Mode place of a character.
//
// Both Hokages are complete fighters in naruto.bf (Ninja_First.wow, Ninja_Second.wow: model,
// moves, voice), but Fight Mode has no entry for them. A character's place is taken over in
// three steps:
//  - Its versus world list (<name>_Versus.wol, from WOLInfo.bin, read once at boot by
//    sub_82BC7230 into a sorted key -> list map at +412 of its object) loads the Hokage's
//    fighter world instead of the character's own one. Everything else the list loads stays:
//    intro scenes, sounds, the versus artwork.
//  - The Hokage's fighter points at the character's Fight Mode identity instead of his story
//    one (the identity holds the character id, the name and the fight setup). The game's LZO
//    decompressor, sub_82BBB400(src, src_len, dst, &dst_len), unpacks every bigfile chunk;
//    right after it, the identity key next to the Hokage's sound key is replaced. Data in the
//    bigfile is little endian.
//  - The character's identity (All_NinjaIdentities.wow) gets the Hokage's name text, so the
//    fight shows FIRST HOKAGE or SECOND HOKAGE. That file is unpacked once, at boot, and Story
//    Mode uses it too: there the replaced character also goes by the Hokage's name.
// Naruto cannot be replaced: every versus list loads his fighter, as the game's fallback.
// A fighter choice applies at the next fight, the name after a restart. Story Mode's fight
// against the Hokages is recognized by the puppet worlds it loads and left as it is.
//
// For testing, REX_DATA_PATCH=<file> applies extra edits to every unpacked chunk, one per line:
// "<find hex> <replace hex>" (same length).

#include "hokage_fighters.h"

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

REXCVAR_DEFINE_STRING(first_hokage, "off", "Game",
                      "Fight Mode character the First Hokage replaces, or off");
REXCVAR_DEFINE_STRING(second_hokage, "off", "Game",
                      "Fight Mode character the Second Hokage replaces, or off");

extern "C" void __imp__sub_82BBB400(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82BC7230(PPCContext& __restrict, uint8_t*);

namespace hokage {

const std::array<Character, 29>& Roster() {
  static const std::array<Character, 29> roster = {{
      {"sasuke", "SASUKE", 11, 0x66001F71, 0x6E001250, 0x7900348E, {0x00000000, 0x00000000, 0x0000000B}, 0x626B},
      {"sakura", "SAKURA", 10, 0x66001F80, 0x6E00124D, 0x7900348C, {0x00000000, 0x00000000, 0x0000000A}, 0x626C},
      {"kakashi", "KAKASHI", 12, 0x66001F8E, 0x6E001238, 0x79003486, {0xFFFFFFFF, 0x00000004, 0x0000000C}, 0x626D},
      {"rock_lee", "ROCK LEE", 15, 0x6600200B, 0x6E00124A, 0x7900348B, {0x00000000, 0x00000000, 0x0000000F}, 0x627A},
      {"neji", "NEJI", 2, 0x66001F9E, 0x6E001244, 0x79003489, {0x00000000, 0x00000000, 0x00000002}, 0x58D9},
      {"gaara", "GAARA", 5, 0x66001FBA, 0x6E00120E, 0x79003DC2, {0xFFFFFFFF, 0x00000001, 0x00000005}, 0x6278},
      {"shikamaru", "SHIKAMARU", 33, 0x66002028, 0x6E001256, 0x7900348F, {0x00000000, 0x00000000, 0x00000021}, 0x58DA},
      {"choji", "CHOJI", 36, 0x66001FAC, 0x6600107E, 0x79003483, {0x00000000, 0x00000000, 0x00000024}, 0x6273},
      {"kiba", "KIBA", 9, 0x66001FEE, 0x6E00123B, 0x79003487, {0x00000000, 0x00000000, 0x00000009}, 0x6275},
      {"temari", "TEMARI", 37, 0x66002036, 0x66001037, 0x79003490, {0xFFFFFFFF, 0x00000002, 0x00000025}, 0x6277},
      {"haku", "HAKU", 7, 0x66001FD2, 0x6E001232, 0x79003484, {0xFFFFFFFF, 0x00000001, 0x00000007}, 0x626A},
      {"zabuza", "ZABUZA", 14, 0x66002044, 0x6E001253, 0x79003491, {0xFFFFFFFF, 0x00000004, 0x0000000E}, 0x6269},
      {"orochimaru", "OROCHIMARU", 6, 0x66001FFD, 0x6E001247, 0x7900348A, {0xFFFFFFFF, 0x00000005, 0x00000006}, 0x626F},
      {"kabuto", "KABUTO", 43, 0x66002366, 0x66002372, 0x40004059, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002B}, 0x58DB},
      {"jiraiya", "JIRAIYA", 34, 0x66001FE0, 0x84001440, 0x79003485, {0xFFFFFFFF, 0x00000005, 0x00000022}, 0x626E},
      {"tsunade", "TSUNADE", 42, 0x66002319, 0x66002324, 0x40004058, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002A}, 0x6270},
      {"sarutobi", "SARUTOBI", 35, 0x6600201A, 0x840014EB, 0x7900348D, {0xFFFFFFFF, 0x00000004, 0x00000023}, 0x6279},
      {"itachi", "ITACHI", 44, 0x660068D4, 0x660068DF, 0x79003DC3, {0xFFFFFFFF, 0x00000000, 0x0000002C}, 0x627C},
      {"kisame", "KISAME", 49, 0x6600939D, 0x660093A8, 0x79003DC4, {0xFFFFFFFF, 0x00000000, 0x00000031}, 0x627D},
      {"kimimaro", "KIMIMARO", 41, 0x660021D6, 0x660021E1, 0x40004053, {0x00000032, 0xFFFFFFFF, 0x00000029}, 0x6268},
      {"jirobo", "JIROBO", 45, 0x6600623B, 0x66006246, 0x40004054, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002D}, 0x6271},
      {"kidomaru", "KIDOMARU", 48, 0x66007C92, 0x66007C9D, 0x40004056, {0xFFFFFFFF, 0x00000001, 0x00000030}, 0x6267},
      {"sakon", "SAKON", 47, 0x66006897, 0x660068A2, 0x40004057, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002F}, 0x6272},
      {"tayuya", "TAYUYA", 46, 0x6600686A, 0x66006875, 0x40004055, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002E}, 0x6266},
      {"nine_tails_naruto", "NINE-TAILS NARUTO", 89, 0x2D002463, 0x2D00246E, 0x4000405A, {0xFFFFFFFF, 0xFFFFFFFF, 0x00000059}, 0x62C2},
      {"curse_mark_sasuke", "CURSE MARK SASUKE", 90, 0x2D0024C6, 0x2D0024D1, 0x4000405B, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000005A}, 0x62C3},
      {"green_naruto", "GREEN NARUTO", 97, 0x2D0032C6, 0x2D0032D1, 0x4000405C, {0x00000027, 0xFFFFFFFF, 0x00000061}, 0x6322},
      {"chunin_exam_sasuke", "CHUNIN EXAM SASUKE", 98, 0x2D0032F2, 0x2D0032FD, 0x40003F11, {0x00000000, 0x00000000, 0x00000062}, 0x6360},
      {"anbu_itachi", "ANBU ITACHI", 99, 0x2D00363B, 0x2D003646, 0x4000405D, {0xFFFFFFFF, 0xFFFFFFFF, 0x00000063}, 0x632F},
  }};
  return roster;
}

}  // namespace hokage

namespace {
using hokage::Character;
using hokage::Roster;

struct Hokage {
  const char* setting;
  uint32_t fighter_world;  // Ninja_<name>.wow
  uint32_t identity;       // NinjaIdentity_<name>.gao, his story identity
  uint32_t sound;          // NinjaSound_<name>.gao, stored right after the identity key
  uint32_t name_text;      // text id of his name
};

constexpr std::array<Hokage, 2> kHokages = {{
    {hokage::kFirstSetting, 0x2D002A99, 0x2D002AA4, 0x2D002AA7, 0x5E50},
    {hokage::kSecondSetting, 0x2D002AAF, 0x2D002ABB, 0x2D002ABE, 0x5E51},
}};

// Puppet_First.wow and Puppet_Second.wow, loaded only by Story Mode's Hokage fight.
constexpr std::array<uint32_t, 2> kStoryPuppets = {0x2D001413, 0x2D00141C};

uint8_t* g_base = nullptr;
std::mutex g_mutex;
std::array<uint32_t, 29> g_versus_at{};  // guest address of each versus list record
uint32_t g_world = 0;        // the world file being unpacked
bool g_story_fight = false;  // set while Story Mode's Hokage fight loads

uint32_t Load32(uint8_t* base, uint32_t address) {
  return _byteswap_ulong(*reinterpret_cast<uint32_t*>(base + address));
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  *reinterpret_cast<uint32_t*>(base + address) = _byteswap_ulong(value);
}

// The character a Hokage replaces, or nullptr. The Second gives way when both pick the same one.
const Character* Target(size_t hokage) {
  const std::string value = rex::cvar::GetFlagByName(kHokages[hokage].setting);
  for (const Character& character : Roster()) {
    if (value != character.value) continue;
    if (hokage == 1 && rex::cvar::GetFlagByName(kHokages[0].setting) == value) return nullptr;
    return &character;
  }
  return nullptr;
}

// Points each versus list at the fighter world that should fight in its place.
void ApplyLists() {
  std::lock_guard lock(g_mutex);
  if (!g_base) return;
  const std::array<const Character*, 2> targets = {Target(0), Target(1)};
  for (size_t c = 0; c < Roster().size(); ++c) {
    const Character& character = Roster()[c];
    const uint32_t record = g_versus_at[c];
    if (!record) continue;
    uint32_t wanted = character.fighter_world;
    for (size_t h = 0; h < kHokages.size(); ++h) {
      if (targets[h] == &character) wanted = kHokages[h].fighter_world;
    }
    const uint32_t count = Load32(g_base, record), keys = Load32(g_base, record + 4);
    for (uint32_t i = 0; i < count && i < 4096; ++i) {
      const uint32_t key = Load32(g_base, keys + 4 * i);
      bool fighter = key == character.fighter_world;
      for (const Hokage& h : kHokages) fighter = fighter || key == h.fighter_world;
      if (fighter && key != wanted) Store32(g_base, keys + 4 * i, wanted);
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
// entries. The versus list records are remembered there, then set for the current choice.
REX_HOOK_RAW(sub_82BC7230) {
  const uint32_t object = ctx.r3.u32;
  __imp__sub_82BC7230(ctx, base);
  const uint32_t entries = Load32(base, object + 412), count = Load32(base, object + 416);
  {
    std::lock_guard lock(g_mutex);
    g_base = base;
    int found = 0;
    for (uint32_t i = 0; entries && i < count; ++i) {
      const uint32_t key = Load32(base, entries + 8 * i), record = Load32(base, entries + 8 * i + 4);
      for (size_t c = 0; c < Roster().size(); ++c) {
        if (key == Roster()[c].versus_list) {
          g_versus_at[c] = record;
          ++found;
        }
      }
    }
    REXLOG_INFO("[hokage] {} of {} versus lists found", found, Roster().size());
  }
  static std::once_flag callbacks;
  std::call_once(callbacks, [] {
    for (const Hokage& h : kHokages) {
      rex::cvar::RegisterChangeCallback(h.setting, [](std::string_view, std::string_view) { ApplyLists(); });
    }
  });
  ApplyLists();
}

REX_HOOK_RAW(sub_82BBB400) {
  const uint32_t dst = ctx.r5.u32, dst_len = ctx.r6.u32;
  __imp__sub_82BBB400(ctx, base);
  const uint32_t length = Load32(base, dst_len);
  uint8_t* data = base + dst;
  // A world file's first chunk starts with {size, 0xEEFFC099, its own key, ".wow"}; later chunks
  // belong to the same world.
  uint32_t marker = 0, key = 0, ext = 0;
  if (length >= 16) {
    std::memcpy(&marker, data + 4, 4);
    std::memcpy(&key, data + 8, 4);
    std::memcpy(&ext, data + 12, 4);
  }
  if (marker == 0xEEFFC099 && ext == 0x776F772E) {
    // Story Mode's Hokage fight loads Puppet_First/Puppet_Second, then Ninja_First and
    // Ninja_Second; Fight Mode never loads the puppets. That fight keeps the story identities.
    if (key == kStoryPuppets[0] || key == kStoryPuppets[1]) g_story_fight = true;
    else if (g_world == kHokages[1].fighter_world) g_story_fight = false;
    g_world = key;
  }
  for (size_t h = 0; h < kHokages.size(); ++h) {
    const Character* target = Target(h);
    if (!target) continue;
    const Hokage& hokage = kHokages[h];
    // The Hokage's fighter keeps his own voice (sound key) and takes the character's identity.
    if (!g_story_fight && g_world == hokage.fighter_world) {
      uint8_t find[12], replace[12];
      PutLE(find, hokage.identity);
      PutLE(find + 4, hokage.sound);
      PutLE(find + 8, 0x70005A80);
      std::memcpy(replace, find, sizeof(find));
      PutLE(replace, target->identity);
      Replace(data, length, find, replace, sizeof(find), dst);
    }
    {
      uint8_t find[16], replace[16];
      for (size_t i = 0; i < 3; ++i) PutLE(find + 4 * i, target->name_lead[i]);
      PutLE(find + 12, target->name_text);
      std::memcpy(replace, find, sizeof(find));
      PutLE(replace + 12, hokage.name_text);
      Replace(data, length, find, replace, sizeof(find), dst);
    }
  }
  for (const Patch& patch : TestPatches()) {
    Replace(data, length, patch.find.data(), patch.replace.data(), patch.find.size(), dst);
  }
}
