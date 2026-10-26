// Hidden fighters (PC Options > Cheats > Hidden Characters & Skins): the First and the Second
// Hokage lend their look to a Fight Mode character the player picks (a skin: the character keeps
// his own moves).

#pragma once

#include <array>
#include <cstdint>

namespace hokage {

// A Fight Mode character a Hokage can replace.
struct Character {
  const char* value;  // setting value
  const char* name;   // shown in the menu
  uint32_t id;        // the game's character id
  uint32_t fighter_world;  // <name>.wow, the fighter itself
  uint32_t identity;       // NinjaIdentity_<name>.gao
  uint32_t versus_list;    // <name>_Versus.wol, what Fight Mode loads for the character
  std::array<uint32_t, 3> name_lead;  // the identity variables just before its name text id
  uint32_t name_text;
  std::array<uint32_t, 3> shape;  // the look its Ninja_<name>.gao links: shape group and its two parts
};

const std::array<Character, 29>& Roster();

// The settings: "off" or a Character::value.
constexpr const char* kFirstSetting = "first_hokage";
constexpr const char* kSecondSetting = "second_hokage";

}  // namespace hokage
