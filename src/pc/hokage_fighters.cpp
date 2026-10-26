// Hokage skins (PC Options > Cheats > Hidden Characters & Skins): the First and the Second
// Hokage, who only fight in Story Mode, lend their look to a Fight Mode character, who keeps
// his own moves, jutsus and voice.
//
// The Hokages' fighter worlds in naruto.bf (Ninja_First.wow, Ninja_Second.wow) only have the
// story boss moves, so the character stays the fighter and only his look changes, in three steps:
//  - His versus world list (<name>_Versus.wol, from WOLInfo.bin, read once at boot by
//    sub_82BC7230 into a sorted key -> list map at +412 of its object) also loads the Hokage's
//    look, his story puppet world (Puppet_First.wow, Puppet_Second.wow, a tenth of his fighter
//    world: a whole fighter world more stopped Kakashi's fight from loading), in the place of a
//    story scene every list carries and no fight plays (SE_JN_CBridge_Success.wow), some 25
//    worlds before the character's own one, which comes last.
//  - The character's fighter (Ninja_<name>.gao) links his look as three keys: a shape group and
//    its two parts (the game's own outfits, such as Sasuke's funeral one, change these; the
//    group alone does not change the look). The game's LZO decompressor,
//    sub_82BBB400(src, src_len, dst, &dst_len), unpacks every bigfile chunk; right after it,
//    those keys become the Hokage's, only within kSkinReach worlds after his puppet world, so
//    Story Mode keeps every character's own look (its Hokage fight loads the Hokages' fighter
//    worlds right after their puppets, which ends that). Data in the bigfile is little endian.
//  - The character's identity (All_NinjaIdentities.wow) gets the Hokage's name text, so the
//    fight shows FIRST HOKAGE or SECOND HOKAGE. That file is unpacked once, at boot, and Story
//    Mode uses it too: there the character also goes by the Hokage's name.
// Naruto cannot be replaced: every versus list loads his fighter, as the game's fallback.
// A choice applies at the next fight, the name after a restart.
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
#include <string_view>
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
      {"sasuke", "SASUKE", 11, 0x66001F71, 0x6E001250, 0x7900348E, {0x00000000, 0x00000000, 0x0000000B}, 0x626B, {0xBE00076F, 0xBE000764, 0xBE00076C}},
      {"sakura", "SAKURA", 10, 0x66001F80, 0x6E00124D, 0x7900348C, {0x00000000, 0x00000000, 0x0000000A}, 0x626C, {0xB00003DA, 0xB00003D0, 0xB00003D8}},
      {"kakashi", "KAKASHI", 12, 0x66001F8E, 0x6E001238, 0x79003486, {0xFFFFFFFF, 0x00000004, 0x0000000C}, 0x626D, {0x2D0008CB, 0x2D0008C5, 0x2D0008C9}},
      {"rock_lee", "ROCK LEE", 15, 0x6600200B, 0x6E00124A, 0x7900348B, {0x00000000, 0x00000000, 0x0000000F}, 0x627A, {0xBE00192E, 0xBE001924, 0xBE00192C}},
      {"neji", "NEJI", 2, 0x66001F9E, 0x6E001244, 0x79003489, {0x00000000, 0x00000000, 0x00000002}, 0x58D9, {0xBE004929, 0xBE00491C, 0xBE004926}},
      {"gaara", "GAARA", 5, 0x66001FBA, 0x6E00120E, 0x79003DC2, {0xFFFFFFFF, 0x00000001, 0x00000005}, 0x6278, {0x2D00314A, 0xBE006453, 0xBE00645B}},
      {"shikamaru", "SHIKAMARU", 33, 0x66002028, 0x6E001256, 0x7900348F, {0x00000000, 0x00000000, 0x00000021}, 0x58DA, {0x2D00200F, 0xBE004AF0, 0xBE004AF8}},
      {"choji", "CHOJI", 36, 0x66001FAC, 0x6600107E, 0x79003483, {0x00000000, 0x00000000, 0x00000024}, 0x6273, {0x2D0027F4, 0xBE0056E9, 0xBE0056EF}},
      {"kiba", "KIBA", 9, 0x66001FEE, 0x6E00123B, 0x79003487, {0x00000000, 0x00000000, 0x00000009}, 0x6275, {0x2D0027F2, 0xBE005848, 0xBE005850}},
      {"temari", "TEMARI", 37, 0x66002036, 0x66001037, 0x79003490, {0xFFFFFFFF, 0x00000002, 0x00000025}, 0x6277, {0xBE005CCD, 0xBE005BF8, 0xBE005C00}},
      {"haku", "HAKU", 7, 0x66001FD2, 0x6E001232, 0x79003484, {0xFFFFFFFF, 0x00000001, 0x00000007}, 0x626A, {0x2D002A2B, 0xBE005CB8, 0xBE005CC0}},
      {"zabuza", "ZABUZA", 14, 0x66002044, 0x6E001253, 0x79003491, {0xFFFFFFFF, 0x00000004, 0x0000000E}, 0x6269, {0x2D000639, 0x2D000631, 0x2D000637}},
      {"orochimaru", "OROCHIMARU", 6, 0x66001FFD, 0x6E001247, 0x7900348A, {0xFFFFFFFF, 0x00000005, 0x00000006}, 0x626F, {0x2D0034AD, 0xBE004DAE, 0xBE004DB8}},
      {"kabuto", "KABUTO", 43, 0x66002366, 0x66002372, 0x40004059, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002B}, 0x58DB, {0xBE001241, 0xBE001238, 0xBE00123E}},
      {"jiraiya", "JIRAIYA", 34, 0x66001FE0, 0x84001440, 0x79003485, {0xFFFFFFFF, 0x00000005, 0x00000022}, 0x626E, {0xBE003C19, 0xBE003C0E, 0xBE003C16}},
      {"tsunade", "TSUNADE", 42, 0x66002319, 0x66002324, 0x40004058, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002A}, 0x6270, {0xBE000F4A, 0xBE000F40, 0xBE000F48}},
      {"sarutobi", "SARUTOBI", 35, 0x6600201A, 0x840014EB, 0x7900348D, {0xFFFFFFFF, 0x00000004, 0x00000023}, 0x6279, {0xBE004EFD, 0xBE004EF5, 0xBE004EFB}},
      {"itachi", "ITACHI", 44, 0x660068D4, 0x660068DF, 0x79003DC3, {0xFFFFFFFF, 0x00000000, 0x0000002C}, 0x627C, {0xBE002D8F, 0xBE002D86, 0xBE002D8E}},
      {"kisame", "KISAME", 49, 0x6600939D, 0x660093A8, 0x79003DC4, {0xFFFFFFFF, 0x00000000, 0x00000031}, 0x627D, {0xBE0050C3, 0xBE003B20, 0xBE003B28}},
      {"kimimaro", "KIMIMARO", 41, 0x660021D6, 0x660021E1, 0x40004053, {0x00000032, 0xFFFFFFFF, 0x00000029}, 0x6268, {0xBE00195D, 0xBE000C44, 0xBE000C4E}},
      {"jirobo", "JIROBO", 45, 0x6600623B, 0x66006246, 0x40004054, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002D}, 0x6271, {0xBE0023E2, 0xBE0023D9, 0xBE0023DF}},
      {"kidomaru", "KIDOMARU", 48, 0x66007C92, 0x66007C9D, 0x40004056, {0xFFFFFFFF, 0x00000001, 0x00000030}, 0x6267, {0xBE0041CD, 0xBE00339F, 0xBE0041C9}},
      {"sakon", "SAKON", 47, 0x66006897, 0x660068A2, 0x40004057, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002F}, 0x6272, {0xBE002FAC, 0xBE002FA1, 0xBE002FAB}},
      {"tayuya", "TAYUYA", 46, 0x6600686A, 0x66006875, 0x40004055, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000002E}, 0x6266, {0xBE002E7A, 0xBE002E72, 0xBE002E78}},
      {"nine_tails_naruto", "NINE-TAILS NARUTO", 89, 0x2D002463, 0x2D00246E, 0x4000405A, {0xFFFFFFFF, 0xFFFFFFFF, 0x00000059}, 0x62C2, {0x2D00245D, 0x2D002451, 0x2D00245B}},
      {"curse_mark_sasuke", "CURSE MARK SASUKE", 90, 0x2D0024C6, 0x2D0024D1, 0x4000405B, {0xFFFFFFFF, 0xFFFFFFFF, 0x0000005A}, 0x62C3, {0x2D0024C0, 0xBE005547, 0xBE005551}},
      {"green_naruto", "GREEN NARUTO", 97, 0x2D0032C6, 0x2D0032D1, 0x4000405C, {0x00000027, 0xFFFFFFFF, 0x00000061}, 0x6322, {0x2D0032C0, 0xBE006BFE, 0xBE006C06}},
      {"chunin_exam_sasuke", "CHUNIN EXAM SASUKE", 98, 0x2D0032F2, 0x2D0032FD, 0x40003F11, {0x00000000, 0x00000000, 0x00000062}, 0x6360, {0x2D00371D, 0xBE000764, 0xBE006D00}},
      {"anbu_itachi", "ANBU ITACHI", 99, 0x2D00363B, 0x2D003646, 0x4000405D, {0xFFFFFFFF, 0xFFFFFFFF, 0x00000063}, 0x632F, {0x2D003153, 0xBE00616C, 0x5E005EDD}},
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
  uint32_t puppet_world;   // Puppet_<name>.wow, his look
  uint32_t name_text;      // text id of his name
  std::array<uint32_t, 3> shape;  // his look, as Character::shape
};

constexpr std::array<Hokage, 2> kHokages = {{
    {hokage::kFirstSetting, 0x2D002A99, 0x2D001413, 0x5E50, {0x2D00140D, 0xBE0045BC, 0xBE0045C4}},
    {hokage::kSecondSetting, 0x2D002AAF, 0x2D00141C, 0x5E51, {0xBE004A1D, 0xBE004A12, 0xBE004A1A}},
}};

// The world in each versus list whose place loads the Hokage's look: SexyGirls.wow, the effects
// of Naruto's Sexy Jutsu (Naruto's own lists keep it), else SE_JN_CBridge_Success.wow, a story
// scene no fight plays.
constexpr uint32_t kSkinSlotWorld = 0xA00007FA;
constexpr uint32_t kNarutoSkinSlotWorld = 0x4F003E26;
// Worlds after a Hokage's puppet world in which a character can still take his look.
constexpr int kSkinReach = 40;

uint8_t* g_base = nullptr;
std::mutex g_mutex;
std::array<uint32_t, 29> g_versus_at{};  // guest address of each versus list record
std::array<uint32_t, 29> g_skin_slot{};  // index of the skin slot in each versus list, + 1
std::array<uint32_t, 29> g_skin_slot_world{};  // the world the skin slot loads without a skin
uint32_t g_world = 0;                  // the world file being unpacked
std::array<int, 2> g_skin_reach{};     // worlds left in which a character takes the Hokage's look

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

// Makes each versus list load the Hokage whose skin its character wears, or the story scene back.
void ApplyLists() {
  std::lock_guard lock(g_mutex);
  if (!g_base) return;
  const std::array<const Character*, 2> targets = {Target(0), Target(1)};
  for (size_t c = 0; c < Roster().size(); ++c) {
    const Character& character = Roster()[c];
    const uint32_t record = g_versus_at[c];
    if (!record || !g_skin_slot[c]) continue;
    uint32_t wanted = g_skin_slot_world[c];
    for (size_t h = 0; h < kHokages.size(); ++h) {
      if (targets[h] == &character) wanted = kHokages[h].puppet_world;
    }
    Store32(g_base, Load32(g_base, record + 4) + 4 * (g_skin_slot[c] - 1), wanted);
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

// Links the Hokage's look in the character's fighter object (Ninja_<name>.gao) only, not in his
// outfit objects (Ninja_<name>_Shape.gao and the like, which carry the same three keys): a gao
// chunk starts with ".gao", its name is at +20 and the look's keys at +127 + the name's length.
void ReplaceFighterLook(uint8_t* data, uint32_t length, const uint8_t* find, const uint8_t* replace, uint32_t dst) {
  for (uint32_t i = 0; i + 12 <= length; ++i) {
    if (std::memcmp(data + i, find, 12) != 0) continue;
    for (uint32_t name_length = 5; name_length < 64 && name_length + 127 <= i; ++name_length) {
      const uint8_t* gao = data + i - 127 - name_length;
      const std::string_view name(reinterpret_cast<const char*>(gao + 20), name_length);
      if (std::memcmp(gao, ".gao", 4) != 0 || gao[20 + name_length] != 0 || name.find('\0') != name.npos) continue;
      if (name.rfind("Ninja_", 0) != 0 || name.find("hape") != name.npos) break;
      std::memcpy(data + i, replace, 12);
      REXLOG_INFO("[hokage] {} takes the Hokage's look ({:08X}+{:X})", name, dst, i);
      break;
    }
  }
}
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
        if (key != Roster()[c].versus_list) continue;
        g_versus_at[c] = record;
        ++found;
        const uint32_t list_count = Load32(base, record), keys = Load32(base, record + 4);
        for (uint32_t k = 0; k < list_count && k < 4096; ++k) {
          const bool naruto = std::string_view(Roster()[c].value).find("naruto") != std::string_view::npos;
          g_skin_slot_world[c] = naruto ? kNarutoSkinSlotWorld : kSkinSlotWorld;
          if (Load32(base, keys + 4 * k) == g_skin_slot_world[c]) g_skin_slot[c] = k + 1;
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
    for (size_t h = 0; h < kHokages.size(); ++h) {
      if (key == kHokages[h].puppet_world) g_skin_reach[h] = kSkinReach;
      else if (key == kHokages[h].fighter_world) g_skin_reach[h] = 0;
      else if (g_skin_reach[h] > 0) --g_skin_reach[h];
    }
    g_world = key;
  }
  for (size_t h = 0; h < kHokages.size(); ++h) {
    const Character* target = Target(h);
    if (!target) continue;
    const Hokage& hokage = kHokages[h];
    // The character's fighter links the Hokage's look.
    if (g_skin_reach[h] > 0 && g_world == target->fighter_world) {
      uint8_t find[12], replace[12];
      for (size_t i = 0; i < 3; ++i) {
        PutLE(find + 4 * i, target->shape[i]);
        PutLE(replace + 4 * i, hokage.shape[i]);
      }
      ReplaceFighterLook(data, length, find, replace, dst);
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
