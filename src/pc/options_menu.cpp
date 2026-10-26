#include "options_menu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string_view>
#include <utility>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include "free_camera.h"
#include "hokage_fighters.h"
#include "imgui_key_map.inc"
#include "input_layout.h"
#include "input_layout_core.h"
#include "pad_input.h"

ImFont* g_menu_font = nullptr;

namespace {

// Colors sampled from the game's own Options screen.
constexpr ImU32 kBlue = IM_COL32(32, 146, 255, 255);
constexpr ImU32 kOrange = IM_COL32(255, 140, 24, 255);
constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kOutline = IM_COL32(6, 12, 38, 255);

constexpr int kVisibleRows = 11;

// The Fight Mode select grid as the game lays it out (5 columns, 6 rows), by setting value;
// Naruto (first cell) cannot be replaced. kGridOff is the OFF button beside the grid.
constexpr int kGridColumns = 5;
constexpr int kGridRows = 6;
constexpr int kGridOff = kGridColumns * kGridRows;
constexpr std::array<const char*, kGridOff> kGridOrder = {
    nullptr,          "sasuke",      "sakura",           "kakashi",       "haku",
    "neji",           "kiba",        "shikamaru",        "choji",         "rock_lee",
    "orochimaru",     "kabuto",      "jiraiya",          "tsunade",       "sarutobi",
    "kisame",         "zabuza",      "itachi",           "temari",        "gaara",
    "jirobo",         "tayuya",      "sakon",            "kidomaru",      "kimimaro",
    "nine_tails_naruto", "curse_mark_sasuke", "green_naruto", "chunin_exam_sasuke", "anbu_itachi"};

// The Hidden Characters & Skins rows: each Hokage's name and setting.
constexpr std::array<std::pair<const char*, const char*>, 2> kHokageRows = {{
    {"FIRST HOKAGE", hokage::kFirstSetting}, {"SECOND HOKAGE", hokage::kSecondSetting}}};

// The grid cell of a setting value; kGridOff for "off" or an unknown value.
int GridCellOf(const std::string& value) {
  for (int cell = 0; cell < kGridOff; ++cell) {
    if (kGridOrder[cell] && value == kGridOrder[cell]) return cell;
  }
  return kGridOff;
}

// Layout is designed for 1080p and scaled to the window height.
float g_s = 1.0f;
float S(float value) {
  return value * g_s;
}

ImU32 WithAlpha(ImU32 color, float alpha) {
  const ImU32 a = static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFF) *
                                     std::clamp(alpha, 0.0f, 1.0f));
  return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

float EaseOut(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

ImFont* Font() {
  return g_menu_font ? g_menu_font : ImGui::GetFont();
}

ImVec2 TextSize(float size, const std::string& text) {
  return Font()->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
}

// Text with the dark outline the game's menus use.
void OutlinedText(ImDrawList* dl, ImVec2 pos, float size, ImU32 color, const std::string& text,
                  float alpha, float outline = 2.5f) {
  const float o = S(outline);
  const ImU32 outline_color = WithAlpha(kOutline, alpha);
  static const float kDirs[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1},
                                    {-0.7f, -0.7f}, {0.7f, -0.7f}, {-0.7f, 0.7f}, {0.7f, 0.7f}};
  for (const auto& d : kDirs) {
    dl->AddText(Font(), size, ImVec2(pos.x + d[0] * o, pos.y + d[1] * o), outline_color,
                text.c_str());
  }
  dl->AddText(Font(), size, pos, WithAlpha(color, alpha), text.c_str());
}

// The game's selection arrow: an orange triangle with a white rim.
void Arrow(ImDrawList* dl, ImVec2 center, float size, ImU32 color, float alpha, bool left = false) {
  const float h = size * 0.5f;
  const float dir = left ? -1.0f : 1.0f;
  auto tri = [&](float grow, ImU32 col) {
    dl->AddTriangleFilled(ImVec2(center.x - dir * (h + grow), center.y - h - grow),
                          ImVec2(center.x - dir * (h + grow), center.y + h + grow),
                          ImVec2(center.x + dir * (h + grow * 1.6f), center.y),
                          WithAlpha(col, alpha));
  };
  tri(S(3.0f), kOutline);
  tri(S(1.5f), kWhite);
  tri(0.0f, color);
}

// A controller button icon (A green, B red, X blue, Y yellow) or a key name in brackets.
float Prompt(ImDrawList* dl, float right, float y, const std::string& label, char button,
             const std::string& key, bool pad, float alpha) {
  const float size = S(30.0f);
  const ImVec2 label_size = TextSize(size, label);
  float x = right;
  if (pad) {
    const float r = S(17.0f);
    ImU32 color = IM_COL32(80, 180, 40, 255);
    if (button == 'B') color = IM_COL32(220, 40, 40, 255);
    if (button == 'X') color = IM_COL32(40, 110, 235, 255);
    if (button == 'Y') color = IM_COL32(240, 190, 20, 255);
    const ImVec2 c(x - r, y + label_size.y * 0.5f);
    dl->AddCircleFilled(c, r + S(2.5f), WithAlpha(kWhite, alpha));
    dl->AddCircleFilled(c, r, WithAlpha(color, alpha));
    const std::string letter(1, button);
    const ImVec2 ls = TextSize(S(26.0f), letter);
    dl->AddText(Font(), S(26.0f), ImVec2(c.x - ls.x * 0.5f, c.y - ls.y * 0.5f),
                WithAlpha(kWhite, alpha), letter.c_str());
    x -= r * 2.0f + S(12.0f);
  } else {
    const std::string text = "(" + key + ")";
    const ImVec2 ks = TextSize(size, text);
    OutlinedText(dl, ImVec2(x - ks.x, y), size, kOrange, text, alpha);
    x -= ks.x + S(12.0f);
  }
  OutlinedText(dl, ImVec2(x - label_size.x, y), size, kWhite, label, alpha);
  return x - label_size.x - S(40.0f);
}

// Press modes as stored in naruto.toml and shown in the menu.
const std::vector<std::pair<std::string, std::string>> kModes = {
    {"normal", "NORMAL"}, {"toggle", "TOGGLE"}, {"turbo", "TURBO"}};

std::string ModeLabel(const std::string& mode) {
  for (const auto& [value, label] : kModes) {
    if (value == mode) return label;
  }
  return "NORMAL";
}

constexpr std::array<const char*, layout::kButtonCount> kButtonLabels = {
    "A BUTTON", "B BUTTON", "X BUTTON", "Y BUTTON", "LEFT BUMPER", "RIGHT BUMPER",
    "LEFT TRIGGER", "RIGHT TRIGGER", "LEFT STICK CLICK", "RIGHT STICK CLICK", "BACK", "START",
    "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT"};

// Controller button a keyboard binding drives, for its press mode (empty for stick directions).
std::string ButtonForKeybind(const std::string& cvar) {
  static const std::pair<const char*, const char*> kMap[] = {
      {"keybind_a", "a"}, {"keybind_b", "b"}, {"keybind_x", "x"}, {"keybind_y", "y"},
      {"keybind_left_shoulder", "lb"}, {"keybind_right_shoulder", "rb"},
      {"keybind_left_trigger", "lt"}, {"keybind_right_trigger", "rt"},
      {"keybind_lstick_press", "ls"}, {"keybind_rstick_press", "rs"},
      {"keybind_dpad_up", "up"}, {"keybind_dpad_down", "down"}, {"keybind_dpad_left", "left"},
      {"keybind_dpad_right", "right"}, {"keybind_start", "start"}, {"keybind_back", "back"}};
  for (const auto& [bind, button] : kMap) {
    if (cvar == bind) return button;
  }
  return {};
}

std::string Upper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return text;
}

std::string KeysText(const std::string& keys) {
  if (keys.empty()) return "NONE";
  std::string out;
  for (char c : keys) {
    if (c == ',') {
      out += "  /  ";
    } else {
      out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
  }
  return out;
}

bool RequiresRestart(const std::string& name) {
  // The native renderer applies these to its output right away (gpu::ApplyPresentSettings).
  if (name == "vsync" || name == "brightness" || name == "upscaling" || name == "swap_post_effect") {
    return false;
  }
  for (const auto& entry : rex::cvar::GetRegistry()) {
    if (entry.name == name) {
      return entry.lifecycle != rex::cvar::Lifecycle::kHotReload;
    }
  }
  return false;
}

// "window_size" is not a real setting: it reads and writes window_width/window_height together.
// Settings the engine only reads at startup (such as the graphics card) refuse changes while the
// game runs: the new value is kept here, shown in the menu and saved, and applies after a restart.
std::map<std::string, std::string> g_pending;

std::string GetSetting(const std::string& name) {
  if (const auto it = g_pending.find(name); it != g_pending.end()) return it->second;
  if (name == "window_size") {
    const std::string w = rex::cvar::GetFlagByName("window_width");
    const std::string h = rex::cvar::GetFlagByName("window_height");
    if (w == "0" || h == "0") return "0x0";
    return w + "x" + h;
  }
  return rex::cvar::GetFlagByName(name);
}

// "name = value" with the value written the way TOML expects for the setting's type.
// The controller whose layout the Controller Layout page edits (0 or 1).
int g_pad_layout_index = 0;

// Setting for a controller button's layout: kind is "map" or "mode".
std::string PadCvar(const char* kind, int button, int index = -1) {
  if (index < 0) index = g_pad_layout_index;
  return std::string(index == 0 ? "pad_" : "pad2_") + kind + "_" + layout::kNames[button];
}

std::string TomlLine(const std::string& name) {
  const std::string value = GetSetting(name);
  for (const auto& entry : rex::cvar::GetRegistry()) {
    if (entry.name == name && entry.type != rex::cvar::FlagType::String) {
      return name + " = " + value;
    }
  }
  std::string quoted = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') quoted += '\\';
    quoted += c;
  }
  return name + " = " + quoted + "\"";
}

struct BindingInfo {
  const char* cvar;
  const char* label;
  const char* default_keys;  // the defaults shipped in naruto.toml
};

// Controller buttons in the order players think of them, with what they do in this game.
constexpr std::array<BindingInfo, 25> kBindings{{
    {"keybind_lstick_up", "MOVE FORWARD", "W"},
    {"keybind_lstick_down", "MOVE BACK", "S"},
    {"keybind_lstick_left", "MOVE LEFT", "A"},
    {"keybind_lstick_right", "MOVE RIGHT", "D"},
    {"keybind_rstick_up", "CAMERA UP", "Up"},
    {"keybind_rstick_down", "CAMERA DOWN", "Down"},
    {"keybind_rstick_left", "CAMERA LEFT", "Left"},
    {"keybind_rstick_right", "CAMERA RIGHT", "Right"},
    {"keybind_a", "JUMP / CONFIRM (A)", "Space"},
    {"keybind_b", "BLOCK / BACK (B)", "L"},
    {"keybind_y", "HEAVY ATTACK (Y)", "K"},
    {"keybind_x", "BASIC ATTACK (X)", "J"},
    {"keybind_left_shoulder", "OPEN INVENTORY (LB)", "Q"},
    {"keybind_left_trigger", "JUTSUS / HAND SEALS (LT)", "Shift"},
    {"keybind_right_shoulder", "KUNAI ATTACK (RB)", "E"},
    {"keybind_right_trigger", "SPRINT / GRAB (RT)", "I"},
    {"keybind_lstick_press", "LEFT STICK CLICK", "F"},
    {"keybind_rstick_press", "RIGHT STICK CLICK", "G"},
    {"keybind_dpad_up", "D-PAD UP", "1"},
    {"keybind_dpad_down", "D-PAD DOWN", "2"},
    {"keybind_dpad_left", "D-PAD LEFT", "3"},
    {"keybind_dpad_right", "D-PAD RIGHT", "4"},
    {"keybind_start", "START (PAUSE)", "Return"},
    {"keybind_back", "BACK BUTTON", "Tab"},
    {"keybind_guide", "GUIDE", ""},
}};

}  // namespace

OptionsMenu::OptionsMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path,
                         std::function<void()> on_closed, std::function<void()> on_exit)
    : ImGuiDialog(drawer),
      config_path_(std::move(config_path)),
      on_closed_(std::move(on_closed)),
      on_exit_(std::move(on_exit)) {
  // Buttons already held (such as the Back + Start that opened the menu) are not new presses.
  pad_held_ = ReadPads();
  BuildPages();
}

void OptionsMenu::BuildPages() {
  using Kind = Item::Kind;
  auto choice = [](std::string label, std::string cvar, std::vector<Choice> choices,
                   std::string help = {}) {
    Item item;
    item.kind = Kind::kChoice;
    item.label = std::move(label);
    item.cvar = std::move(cvar);
    item.choices = std::move(choices);
    item.help = std::move(help);
    return item;
  };
  auto toggle = [&](std::string label, std::string cvar, std::string help = {}) {
    return choice(std::move(label), std::move(cvar), {{"OFF", "false"}, {"ON", "true"}},
                  std::move(help));
  };
  auto submenu = [](std::string label, int page) {
    Item item;
    item.kind = Kind::kSubmenu;
    item.label = std::move(label);
    item.submenu = page;
    return item;
  };

  // 0: root
  Page root;
  root.title = "PC OPTIONS";
  root.items = {submenu("DISPLAY", 1), submenu("GRAPHICS", 2), submenu("CONTROLS", 3),
                submenu("AUDIO", 5), submenu("GAME", 6), submenu("CHEATS", 9),
                submenu("SHORTCUT KEYS", 8), submenu("DEBUG", 12)};
  {
    Item exit;
    exit.kind = Kind::kAction;
    exit.label = "EXIT GAME";
    exit.exit = true;
    exit.help = "Closes the game. Progress since your last save is lost.";
    exit.action = [this]() {
      if (!exit_armed_) {
        exit_armed_ = true;
        return;
      }
      Save();
      if (on_exit_) on_exit_();
    };
    root.items.push_back(exit);
  }

  // 1: display
  Page display;
  display.title = "DISPLAY";
  display.items.push_back(choice("DISPLAY MODE", "fullscreen",
                                 {{"WINDOWED", "false"}, {"FULLSCREEN", "true"}}));
  display.items.push_back(choice("WINDOW SIZE", "window_size",
                                 {{"AUTOMATIC", "0x0"},
                                  {"1280X720", "1280x720"},
                                  {"1600X900", "1600x900"},
                                  {"1920X1080", "1920x1080"},
                                  {"2560X1440", "2560x1440"},
                                  {"3840X2160", "3840x2160"}},
                                 "Size of the game window when it is not fullscreen."));
  display.items.push_back(choice("ASPECT RATIO", "aspect_ratio",
                                 {{"16:9", "16:9"}, {"21:9", "21:9"}, {"32:9", "32:9"},
                                  {"AUTOMATIC", "auto"}},
                                 "Wider screens see more of the world. Automatic matches your screen."));
  display.items.push_back(choice("FIELD OF VIEW", "field_of_view",
                                 {{"90%", "0.9"},
                                  {"ORIGINAL", "1"},
                                  {"110%", "1.1"},
                                  {"120%", "1.2"},
                                  {"130%", "1.3"},
                                  {"140%", "1.4"}},
                                 "How much of the world the camera shows. Changes right away."));
  display.items.push_back(toggle("VERTICAL SYNC", "vsync", "Removes tearing. On some screens it lowers the frame rate, so it starts off."));
  display.items.push_back(toggle("G-SYNC / FREESYNC", "d3d12_allow_variable_refresh_rate_and_tearing",
                                 "Lets a variable refresh rate monitor follow the game."));
  display.items.push_back(toggle("FPS COUNTER", "show_fps",
                                 "Shows the frame rate. Press {fps_key} anytime to turn it on or off."));

  // 2: graphics (the native renderer's options; they apply after a restart)
  Page graphics;
  graphics.title = "GRAPHICS";
  graphics.items.push_back(choice("FRAME RATE", "frame_rate",
                                  {{"30 (ORIGINAL)", "30"},
                                   {"60", "60"},
                                   {"120", "120"},
                                   {"144", "144"},
                                   {"165", "165"},
                                   {"240", "240"}},
                                  "Above 30, extra frames are drawn between the game's own for smoother motion."));
  graphics.items.push_back(choice("RENDER RESOLUTION", "internal_resolution_scale",
                                  {{"1280X720 (ORIGINAL)", "1"},
                                   {"1920X1080", "1.5"},
                                   {"2560X1440", "2"},
                                   {"3840X2160", "3"},
                                   {"5120X2880", "4"}},
                                  "Resolution the game is drawn at. Higher is sharper but slower."));
  graphics.items.push_back(choice("ANTI-ALIASING", "msaa_samples",
                                  {{"OFF", "1"}, {"2X MSAA", "2"}, {"4X MSAA (ORIGINAL)", "0"}, {"8X MSAA", "8"}},
                                  "Smooths jagged edges. The game uses 4X; OFF is much faster on laptops and built-in graphics."));
  graphics.items.push_back(toggle("TRANSPARENCY ANTI-ALIASING", "transparency_antialiasing",
                                  "Smooths the edges of leaves, hair and fences (needs MSAA)."));
  graphics.items.push_back(choice("EDGE SMOOTHING", "swap_post_effect",
                                  {{"OFF", "none"}, {"FXAA", "fxaa"}, {"FXAA EXTREME", "fxaa_extreme"}},
                                  "A light, fast smoothing pass on the final picture."));
  graphics.items.push_back(choice("UPSCALING", "upscaling",
                                  {{"BILINEAR", "bilinear"}, {"AMD CAS", "cas"}, {"AMD FSR", "fsr"}},
                                  "How the picture is stretched to your screen. CAS and FSR are sharper."));
  {
    Item bright;
    bright.kind = Kind::kSlider;
    bright.label = "BRIGHTNESS";
    bright.cvar = "brightness";
    bright.min = 0.0f;
    bright.max = 1.0f;
    bright.step = 0.05f;
    bright.help = "0.5 is the game's own brightness.";
    graphics.items.push_back(bright);
  }
  {
    // Graphics card: automatic, or one of the cards found (laptops often have two), by name.
    std::vector<Choice> cards = {{"AUTOMATIC", ""}};
    for (const auto& card : ListGraphicsCards()) {
      const bool listed = std::any_of(cards.begin(), cards.end(),
                                      [&](const Choice& c) { return c.value == card.second; });
      if (!listed) cards.push_back({Upper(card.second), card.second});
    }
    graphics.items.push_back(choice("GRAPHICS CARD", "graphics_device", cards,
                                    "The graphics card that draws the game."));
  }
  graphics.items.push_back(choice("TEXTURE FILTERING", "anisotropic_override",
                                  {{"GAME DEFAULT", "-1"},
                                   {"OFF", "0"},
                                   {"2X", "2"},
                                   {"4X", "3"},
                                   {"8X", "4"},
                                   {"16X", "5"}},
                                  "Keeps textures sharp at an angle (anisotropic filtering)."));

  // 3: controls
  Page controls;
  controls.title = "CONTROLS";
  controls.items.push_back(toggle("KEYBOARD CONTROLS", "mnk_mode",
                                  "Play with the keyboard as if it were a controller."));
  controls.items.push_back(choice("KEYBOARD PLAYER", "keyboard_player",
                                  {{"SHARED WITH PLAYER 1", "auto"},
                                   {"PLAYER 1", "1"},
                                   {"PLAYER 2", "2"}},
                                  "With a controller connected, pick which player the keyboard is."));
  controls.items.push_back(toggle("MOUSE MOVES CAMERA", "mnk_mouse"));
  {
    Item sens;
    sens.kind = Kind::kSlider;
    sens.label = "MOUSE SENSITIVITY";
    sens.cvar = "mnk_sensitivity";
    sens.min = 0.1f;
    sens.max = 5.0f;
    sens.step = 0.1f;
    controls.items.push_back(sens);
  }
  controls.items.push_back(submenu("KEY BINDINGS", 4));
  {
    Item pad = submenu("CONTROLLER LAYOUT", 11);
    pad.help = "Remap any controller button, and make it a toggle or turbo. Each controller has its own.";
    controls.items.push_back(pad);
  }
  {
    Item reset;
    reset.kind = Kind::kAction;
    reset.label = "RESET KEY BINDINGS";
    reset.help = "Puts every key back to the default layout, with normal presses.";
    reset.action = [this]() {
      for (const auto& binding : kBindings) {
        Set(binding.cvar, binding.default_keys);
      }
      for (int b = 0; b < layout::kButtonCount; ++b) {
        Set(std::string("key_mode_") + layout::kNames[b], "normal");
      }
    };
    controls.items.push_back(reset);
  }
  {
    Item reset;
    reset.kind = Kind::kAction;
    reset.label = "RESET CONTROLLER LAYOUT";
    reset.help = "Every button of both controllers back to itself, with normal presses.";
    reset.action = [this]() {
      for (int b = 0; b < layout::kButtonCount; ++b) {
        Set(PadCvar("map", b, 0), layout::kNames[b]);
        Set(PadCvar("mode", b, 0), "normal");
        Set(PadCvar("map", b, 1), layout::kNames[b]);
        Set(PadCvar("mode", b, 1), "normal");
      }
    };
    controls.items.push_back(reset);
  }

  // 4: key bindings
  Page keys;
  keys.title = "KEY BINDINGS";
  for (const auto& binding : kBindings) {
    Item item;
    item.kind = Kind::kKeyBind;
    item.label = binding.label;
    item.cvar = binding.cvar;
    item.default_keys = binding.default_keys;
    const std::string button = ButtonForKeybind(binding.cvar);
    if (!button.empty()) {
      item.mode_cvar = "key_mode_" + button;
      item.help = "Left / right: normal press, toggle (press once to hold) or turbo.";
    }
    keys.items.push_back(item);
  }

  // 5: audio
  Page audio;
  audio.title = "AUDIO";
  audio.items.push_back(toggle("MUTE AUDIO", "audio_mute"));

  // 6: game
  Page game;
  game.title = "GAME";
  game.items.push_back(choice("LANGUAGE", "user_language",
                              {{"ENGLISH", "1"},
                               {"FRENCH", "4"},
                               {"GERMAN", "3"},
                               {"SPANISH", "5"},
                               {"ITALIAN", "6"}},
                              "Menus and subtitles. Japanese voices are in the game's own Options."));

  // Shown the other way round from the setting: ON plays the intros, OFF skips them.
  game.items.push_back(choice("INTRO VIDEOS", "skip_intros", {{"OFF", "true"}, {"ON", "false"}},
                              "The Ubisoft video and the logo screens when the game starts."));
  game.items.push_back(toggle("PC TITLE SCREEN", "show_title_screen",
                              "The white PC title screen with Naruto when the game starts."));

  // 7: controller layout, drawn as a controller
  Page pad;
  pad.title = "CONTROLLER LAYOUT";
  pad.controller = true;

  // 8: shortcut keys
  Page shortcuts;
  shortcuts.title = "SHORTCUT KEYS";
  auto shortcut = [](std::string label, std::string cvar, std::string def, std::string help) {
    Item key;
    key.kind = Kind::kKeyBind;
    key.label = std::move(label);
    key.cvar = std::move(cvar);
    key.default_keys = std::move(def);
    key.single_key = true;
    key.help = std::move(help);
    return key;
  };
  for (const auto& sc : ShortcutList()) {
    shortcuts.items.push_back(shortcut(sc.label, sc.cvar, sc.default_key, sc.help));
  }
  {
    Item reset;
    reset.kind = Kind::kAction;
    reset.label = "RESET SHORTCUT KEYS";
    reset.help = "PC Options back to F1, FPS counter back to F2, every other shortcut cleared.";
    reset.action = [this]() {
      for (const auto& sc : ShortcutList()) {
        Set(sc.cvar, sc.default_key);
      }
    };
    shortcuts.items.push_back(reset);
  }

  // 9: cheats
  Page cheats;
  cheats.title = "CHEATS";
  cheats.items.push_back(toggle("UNLOCK ALL CHARACTERS", "unlock_all_characters",
                                "Every character in Fight Mode. Your save is not changed."));
  cheats.items.push_back(toggle("UNLOCK ALL ARENAS", "unlock_all_arenas",
                                "Every arena in Fight Mode. Your save is not changed."));
  cheats.items.push_back(choice("WEATHER", "weather",
                                {{"GAME", "game"},
                                 {"DAY", "day"},
                                 {"NIGHT", "night"},
                                 {"MORNING", "morning"},
                                 {"OVERCAST", "overcast"},
                                 {"EVENING", "evening"},
                                 {"SUNSET", "sunset"}},
                                "One weather everywhere, fights included. Changes right away. GAME brings back the normal one."));
  {
    Item camera;
    camera.kind = Kind::kAction;
    camera.label = "FREE CAMERA";
    camera.help = "Freezes the game and flies the camera: WASD, Space / Ctrl, Shift, mouse or arrows.";
    camera.summary = []() { return std::string(FreeCameraOn() ? "ON" : "OFF"); };
    camera.action = []() { ToggleFreeCamera(); };
    cheats.items.push_back(camera);
    Item key;
    key.kind = Kind::kKeyBind;
    key.label = "FREE CAMERA KEY";
    key.cvar = "bind_free_camera";
    key.default_keys = "6";
    key.single_key = true;
    key.help = "Turns the free camera on and off during the game.";
    cheats.items.push_back(key);
  }
  cheats.items.push_back(choice("SPEED UP", "speed_up_factor",
                                {{"2X", "2"}, {"3X", "3"}, {"4X", "4"}, {"6X", "6"}, {"8X", "8"}},
                                "How much faster the game runs while the SPEED UP key is held. Vertical sync limits "
                                "it."));
  {
    Item key;
    key.kind = Kind::kKeyBind;
    key.label = "SPEED UP KEY";
    key.cvar = "bind_speed_up";
    key.default_keys = "NumpadPlus";
    key.single_key = true;
    key.help = "Hold it to run the game faster, for cutscenes you cannot skip.";
    cheats.items.push_back(key);
  }
  {
    Item hidden = submenu("HIDDEN CHARACTERS & SKINS", 13);
    hidden.help = "Play the First and the Second Hokage in Fight Mode, in place of a character you pick.";
    hidden.summary = []() {
      const bool on = GetSetting(hokage::kFirstSetting) != "off" || GetSetting(hokage::kSecondSetting) != "off";
      return std::string(on ? "ON" : "OFF");
    };
    cheats.items.push_back(hidden);
  }
  {
    Item auto_jutsu = submenu("AUTO JUTSU", 10);
    auto_jutsu.help = "One button does your character's jutsu, on the keyboard and controllers.";
    auto_jutsu.summary = []() { return GetSetting("auto_jutsu") == "true" ? "ON" : "OFF"; };
    cheats.items.push_back(auto_jutsu);
  }

  // 10: auto jutsu, drawn as a keyboard / controller 1 / controller 2 grid
  Page jutsu;
  jutsu.title = "AUTO JUTSU";
  jutsu.jutsu = true;

  // 11: which controller's layout to edit
  Page pads;
  pads.title = "CONTROLLER LAYOUT";
  for (int index = 0; index < 2; ++index) {
    Item item;
    item.kind = Kind::kAction;
    item.label = "CONTROLLER " + std::to_string(index + 1);
    item.help = "Remap the buttons of controller " + std::to_string(index + 1) + " only.";
    item.summary = [index]() {
      bool connected = false;
      ReadPadRawAt(index, &connected);
      return std::string(connected ? "CONNECTED" : "NOT CONNECTED");
    };
    item.action = [this, index]() {
      g_pad_layout_index = index;
      pages_[7].title = "CONTROLLER " + std::to_string(index + 1) + " LAYOUT";
      OpenPage(7);
    };
    pads.items.push_back(item);
  }

  // 12: debug (the logs to send in with a problem, and the stutter guard)
  Page debug;
  debug.title = "DEBUG";
  debug.items.push_back(toggle("WRITE LOG FILES", "log_to_file",
                               "Saves a log in logs/ every time you play (for fixing problems). Ctrl+F12."));
  debug.items.push_back(toggle("RECORD GRAPHICS LOG", "record_pipelines",
                               "Saves the graphics and the performance of each session to pipeline-runs, to send "
                               "in against stutters. Applies after a restart."));
  debug.items.push_back(toggle("SKIP UNREADY GRAPHICS", "async_shaders",
                               "Fewer stutters on slow graphics chips: an object whose graphics are still being "
                               "prepared is skipped for a moment instead of freezing the game."));

  // 13: hidden characters, one row with a portrait per Hokage
  Page hidden;
  hidden.title = "HIDDEN CHARACTERS & SKINS";
  hidden.tall = true;
  for (size_t h = 0; h < kHokageRows.size(); ++h) {
    const char* setting = kHokageRows[h].second;
    Item item;
    item.kind = Kind::kAction;
    item.label = kHokageRows[h].first;
    item.picture = static_cast<int>(h);
    item.help = "Pick the Fight Mode character he replaces (not Naruto). The fighter changes from "
                "the next fight, the name after a restart.";
    item.summary = [setting]() {
      const std::string value = GetSetting(setting);
      for (const auto& character : hokage::Roster()) {
        if (value == character.value) return std::string("REPLACES ") + character.name;
      }
      return std::string("OFF");
    };
    item.action = [this, h]() {
      picker_hokage_ = h;
      Page& picker = pages_[14];
      picker.title = std::string(kHokageRows[h].first) + " REPLACES";
      const std::string value = GetSetting(kHokageRows[h].second);
      picker.selected = 0;
      for (size_t c = 0; c < hokage::Roster().size(); ++c) {
        if (value == hokage::Roster()[c].value) picker.selected = static_cast<int>(c) + 1;
      }
      picker.scroll = 0;
      grid_cell_ = GridCellOf(value);
      OpenPage(14);
    };
    hidden.items.push_back(item);
  }

  // 14: the character a Hokage replaces (the title names the Hokage), or OFF
  Page picker;
  picker.title = "REPLACES";
  picker.grid = true;
  for (int c = -1; c < static_cast<int>(hokage::Roster().size()); ++c) {
    const std::string value = c < 0 ? "off" : hokage::Roster()[c].value;
    Item item;
    item.kind = Kind::kAction;
    item.label = c < 0 ? "OFF" : hokage::Roster()[c].name;
    item.help = c < 0 ? "Fight Mode keeps every character as it is." : "This character's place in Fight Mode.";
    item.summary = [this, value]() {
      const char* mine = kHokageRows[picker_hokage_].second;
      const char* other = kHokageRows[1 - picker_hokage_].second;
      if (GetSetting(mine) == value) return std::string("SELECTED");
      if (value != "off" && GetSetting(other) == value) return std::string(kHokageRows[1 - picker_hokage_].first);
      return std::string();
    };
    item.action = [this, value]() {
      const char* mine = kHokageRows[picker_hokage_].second;
      const char* other = kHokageRows[1 - picker_hokage_].second;
      Set(mine, value);
      // One Hokage per character: the other one gives the place up.
      if (value != "off" && GetSetting(other) == value) Set(other, "off");
      page_ = page_stack_.back();
      page_stack_.pop_back();
      page_time_ = time_;
      select_time_ = time_;
    };
    picker.items.push_back(item);
  }

  pages_ = {root,      display, graphics, controls, keys,  audio,  game,  pad,
            shortcuts, cheats,  jutsu,    pads,     debug, hidden, picker};

  // The Hokage portraits are optional resources (assets/portraits/, not in the public source).
  HMODULE module = GetModuleHandleW(nullptr);
  auto* immediate = imgui_drawer() ? imgui_drawer()->immediate_drawer() : nullptr;
  for (size_t h = 0; h < pictures_.size() && immediate; ++h) {
    HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(107 + static_cast<int>(h)), MAKEINTRESOURCEW(10));
    HGLOBAL handle = info ? LoadResource(module, info) : nullptr;
    const auto* data = handle ? static_cast<const stbi_uc*>(LockResource(handle)) : nullptr;
    if (!data) continue;
    int w = 0, hgt = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(SizeofResource(module, info)), &w, &hgt,
                                            &channels, 4);
    if (!pixels) continue;
    pictures_[h] = immediate->CreateTexture(static_cast<uint32_t>(w), static_cast<uint32_t>(hgt),
                                            rex::ui::ImmediateTextureFilter::kLinear, false, pixels);
    stbi_image_free(pixels);
  }
}

void OptionsMenu::JumpTo(int page) {
  if (page == page_) return;
  page_stack_.clear();
  if (page != 0) page_stack_.push_back(0);
  if (page == 4 || page == 7 || page == 11) page_stack_.push_back(3);  // these live in Controls
  if (page == 7) page_stack_.push_back(11);  // the layout of the controller picked last
  if (page == 10) page_stack_.push_back(9);              // auto jutsu lives in Cheats
  page_ = page;
  page_time_ = time_;
  select_time_ = time_;
}

void OptionsMenu::RequestClose() {
  if (close_time_ < 0.0) {
    close_time_ = time_;
  }
}

void OptionsMenu::OnClose() {
  if (dirty_) {
    Save();
  }
  if (on_closed_) {
    on_closed_();
  }
}

bool SaveSettings(const std::filesystem::path& config_path, const std::set<std::string>& names) {
  // Rewrites only the lines of the given settings, so the comments and layout of naruto.toml
  // survive; settings missing from the file are appended at the end.
  std::vector<std::string> lines;
  {
    std::ifstream in(config_path);
    for (std::string line; std::getline(in, line);) {
      lines.push_back(line);
    }
  }

  std::set<std::string> written;
  for (auto& line : lines) {
    const auto first = line.find_first_not_of(" \t");
    const auto eq = line.find('=');
    if (first == std::string::npos || line[first] == '#' || eq == std::string::npos) {
      continue;
    }
    std::string name = line.substr(first, eq - first);
    name.erase(name.find_last_not_of(" \t") + 1);
    if (!names.contains(name)) {
      continue;
    }
    std::string comment;
    const auto value_start = line.find_first_not_of(" \t", eq + 1);
    if (value_start != std::string::npos) {
      const auto value_end =
          line[value_start] == '"' ? line.find('"', value_start + 1) : value_start;
      const auto hash = line.find('#', value_end == std::string::npos ? value_start : value_end);
      if (hash != std::string::npos) {
        comment = "  " + line.substr(hash);
      }
    }
    line = TomlLine(name) + comment;
    written.insert(name);
  }
  for (const auto& name : names) {
    if (!written.contains(name)) {
      lines.push_back(TomlLine(name));
    }
  }

  std::ofstream out(config_path, std::ios::trunc);
  if (!out) {
    REXLOG_ERROR("[options] Could not write {}", config_path.string());
    return false;
  }
  for (const auto& line : lines) {
    out << line << '\n';
  }
  return true;
}

void OptionsMenu::Save() {
  if (SaveSettings(config_path_, changed_)) {
    changed_.clear();
    dirty_ = false;
  }
}

void OptionsMenu::Set(const std::string& name, const std::string& value) {
  if (name == "window_size") {
    const auto x = value.find('x');
    Set("window_width", value.substr(0, x));
    Set("window_height", value.substr(x + 1));
    return;
  }
  if (GetSetting(name) == value) {
    return;
  }
  if (rex::cvar::SetFlagByName(name, value)) {
    g_pending.erase(name);
  } else {
    g_pending[name] = value;  // read only at startup: saved now, applied after a restart
    restart_needed_ = true;
  }
  changed_.insert(name);
  if (name == "log_to_file") {
    rex::SetFileLogging(value == "true");
  }
  dirty_ = true;
  flash_ = 1.0f;
  if (name.rfind("pad_", 0) == 0 || name.rfind("pad2_", 0) == 0 ||
      name.rfind("key_mode_", 0) == 0) {
    ReloadInputLayout();
  }
  if (RequiresRestart(name)) {
    restart_needed_ = true;
  }
}

int OptionsMenu::ChoiceIndex(const Item& item) const {
  const std::string value = GetSetting(item.cvar);
  for (size_t i = 0; i < item.choices.size(); ++i) {
    if (item.choices[i].value == value) return static_cast<int>(i);
  }
  // Decimal settings read back as "1.500000": match them by value.
  char* end = nullptr;
  const double number = std::strtod(value.c_str(), &end);
  if (!value.empty() && end && *end == '\0') {
    for (size_t i = 0; i < item.choices.size(); ++i) {
      const auto& choice = item.choices[i].value;
      char* choice_end = nullptr;
      const double option = std::strtod(choice.c_str(), &choice_end);
      if (!choice.empty() && choice_end && *choice_end == '\0' && std::fabs(option - number) < 1e-6) {
        return static_cast<int>(i);
      }
    }
  }
  return -1;
}

std::string OptionsMenu::ValueText(const Item& item) const {
  switch (item.kind) {
    case Item::Kind::kChoice: {
      const int index = ChoiceIndex(item);
      if (index >= 0) return item.choices[index].label;
      const std::string raw = GetSetting(item.cvar);
      return raw == "0x0" ? "AUTOMATIC" : Upper(raw);
    }
    case Item::Kind::kSlider: {
      char text[32];
      std::snprintf(text, sizeof(text), "%.1f",
                    std::atof(GetSetting(item.cvar).c_str()));
      return text;
    }
    case Item::Kind::kKeyBind:
      return KeysText(GetSetting(item.cvar));
    default:
      return {};
  }
}

void OptionsMenu::OpenPage(int page) {
  page_stack_.push_back(page_);
  page_ = page;
  page_time_ = time_;
  select_time_ = time_;
}

void OptionsMenu::Activate(Item& item, int direction) {
  switch (item.kind) {
    case Item::Kind::kChoice: {
      if (item.choices.empty()) return;
      const int count = static_cast<int>(item.choices.size());
      int index = ChoiceIndex(item);
      index = index < 0 ? 0 : (index + direction + count) % count;
      Set(item.cvar, item.choices[index].value);
      for (const auto& mirror : item.mirror) {
        Set(mirror, item.choices[index].value);
      }
      break;
    }
    case Item::Kind::kSlider: {
      float value = static_cast<float>(std::atof(GetSetting(item.cvar).c_str()));
      value = std::clamp(value + item.step * static_cast<float>(direction), item.min, item.max);
      char text[32];
      std::snprintf(text, sizeof(text), "%.2f", value);
      Set(item.cvar, text);
      break;
    }
    case Item::Kind::kSubmenu:
      if (direction > 0) OpenPage(item.submenu);
      break;
    case Item::Kind::kAction:
      if (direction > 0 && item.action) item.action();
      break;
    case Item::Kind::kKeyBind:
      if (direction > 0) {
        capturing_ = item.cvar;
        append_key_ = false;
        capture_frames_ = 0;
      }
      break;
  }
}

OptionsMenu::Nav OptionsMenu::ReadInput(ImGuiIO& io) {
  Nav nav;
  // Keyboard (with key repeat).
  auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  nav.up = key(ImGuiKey_UpArrow) || key(ImGuiKey_W);
  nav.down = key(ImGuiKey_DownArrow) || key(ImGuiKey_S);
  nav.left = key(ImGuiKey_LeftArrow) || key(ImGuiKey_A);
  nav.right = key(ImGuiKey_RightArrow) || key(ImGuiKey_D);
  nav.confirm = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Space, false);
  nav.back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
             ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
  nav.clear = ImGui::IsKeyPressed(ImGuiKey_Delete, false);
  nav.add = ImGui::IsKeyPressed(ImGuiKey_Insert, false);
  if (nav.up || nav.down || nav.left || nav.right || nav.confirm || nav.back) {
    pad_prompts_ = false;
  }

  // Controllers: edge detection plus auto-repeat on the directions.
  const uint32_t held = ReadPads();
  uint32_t pressed = held & ~pad_held_;
  if (pressed & kPadDirs) {
    pad_repeat_at_ = time_ + 0.35;
  } else if ((held & kPadDirs) && time_ >= pad_repeat_at_) {
    pressed |= held & kPadDirs;
    pad_repeat_at_ = time_ + 0.09;
  }
  pad_held_ = held;
  if (pressed) {
    pad_prompts_ = true;
  }
  nav.up |= (pressed & kPadUp) != 0;
  nav.down |= (pressed & kPadDown) != 0;
  nav.left |= (pressed & kPadLeft) != 0;
  nav.right |= (pressed & kPadRight) != 0;
  nav.confirm |= (pressed & kPadA) != 0;
  nav.back |= (pressed & kPadB) != 0;
  nav.start = (pressed & kPadStart) != 0;
  nav.clear |= (pressed & kPadX) != 0;
  nav.add |= (pressed & kPadY) != 0;
  (void)io;
  return nav;
}

void OptionsMenu::Capture(ImGuiIO& io, bool cancel) {
  (void)io;
  ++capture_frames_;
  if (cancel) {
    capturing_.clear();
    return;
  }
  if (capture_frames_ < 2) {
    return;  // the press that started the capture
  }
  std::string pressed;
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && pressed.empty(); ++k) {
    const auto key = static_cast<ImGuiKey>(k);
    if (key == ImGuiKey_Escape ||
        (key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadRStickDown) ||
        (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY)) {
      continue;
    }
    if (ImGui::IsKeyPressed(key, false)) {
      pressed = rex::ui::VirtualKeyToString(ImGuiKeyToVirtualKey(key));
    }
  }
  const char* mouse_names[] = {"LMB", "RMB", "MMB"};
  for (int b = 0; b < 3 && pressed.empty(); ++b) {
    if (ImGui::IsMouseClicked(b)) pressed = mouse_names[b];
  }
  if (!pressed.empty()) {
    // A shortcut key can only do one thing; any other shortcut using it is cleared afterwards.
    for (const auto& sc : ShortcutList()) {
      if (sc.cvar == capturing_) captured_shortcut_key_ = pressed;
    }
    const std::string keys = GetSetting(capturing_);
    Set(capturing_, append_key_ && !keys.empty() && keys != pressed ? keys + "," + pressed
                                                                    : pressed);
    capturing_.clear();
  }
}

void OptionsMenu::HandleInput(const Nav& nav) {
  Page& page = pages_[page_];
  const int count = static_cast<int>(page.items.size());

  if (nav.back || nav.start) {
    if (page_stack_.empty()) {
      RequestClose();
    } else {
      page_ = page_stack_.back();
      page_stack_.pop_back();
      page_time_ = time_;
      select_time_ = time_;
    }
    return;
  }
  if (count == 0) return;

  const int before = page.selected;
  if (nav.up) page.selected = (page.selected + count - 1) % count;
  if (nav.down) page.selected = (page.selected + 1) % count;
  if (page.selected != before) {
    select_time_ = time_;
    exit_armed_ = false;
  }

  Item& item = page.items[page.selected];
  if (item.kind == Item::Kind::kKeyBind && !item.mode_cvar.empty() && (nav.left || nav.right)) {
    // Left / right cycle the press mode of a keyboard button binding.
    const std::string mode = GetSetting(item.mode_cvar);
    int index = 0;
    for (size_t i = 0; i < kModes.size(); ++i) {
      if (kModes[i].first == mode) index = static_cast<int>(i);
    }
    const int count_modes = static_cast<int>(kModes.size());
    index = (index + (nav.right ? 1 : count_modes - 1)) % count_modes;
    Set(item.mode_cvar, kModes[index].first);
  } else {
    if (nav.left) Activate(item, -1);
    if (nav.right && item.kind != Item::Kind::kAction && item.kind != Item::Kind::kKeyBind) {
      Activate(item, +1);
    }
  }
  if (nav.confirm) Activate(item, +1);
  if (item.kind == Item::Kind::kKeyBind) {
    if (nav.clear) Set(item.cvar, "");
    if (nav.add && !item.single_key) {
      capturing_ = item.cvar;
      append_key_ = true;
      capture_frames_ = 0;
    }
  }
}

void OptionsMenu::HandleMouse(ImGuiIO& io) {
  Page& page = pages_[page_];
  const bool moved = io.MousePos.x != last_mouse_x_ || io.MousePos.y != last_mouse_y_;
  last_mouse_x_ = io.MousePos.x;
  last_mouse_y_ = io.MousePos.y;
  if (hover_row_ >= 0 && moved && hover_row_ != page.selected) {
    page.selected = hover_row_;
    select_time_ = time_;
  }
  if (io.MouseWheel != 0.0f && !page.items.empty()) {
    const int count = static_cast<int>(page.items.size());
    page.selected = std::clamp(page.selected - static_cast<int>(io.MouseWheel), 0, count - 1);
    select_time_ = time_;
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    Nav nav;
    nav.back = true;
    HandleInput(nav);
    return;
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hover_row_ >= 0 &&
      hover_row_ == page.selected) {
    // Clicking the left half of a value steps back, anywhere else steps forward.
    const float value_x = S(150.0f) + S(760.0f);
    const bool left_half = io.MousePos.x > value_x && io.MousePos.x < value_x + S(220.0f);
    Activate(page.items[page.selected], left_half ? -1 : +1);
  }
}

void OptionsMenu::OnDraw(ImGuiIO& io) {
  time_ += io.DeltaTime;
  flash_ = std::max(0.0f, flash_ - io.DeltaTime * 3.0f);
  g_s = std::max(0.5f, io.DisplaySize.y / 1080.0f);

  if (close_time_ < 0.0) {
    if (!capturing_.empty()) {
      const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                          ((ReadPads() & kPadB) != 0 && (pad_held_ & kPadB) == 0);
      pad_held_ = ReadPads();
      // Every shortcut is switched off while a key is being bound, so pressing it doesn't also
      // open an overlay or the menu.
      if (suspended_binds_.empty()) {
        for (const auto& sc : ShortcutList()) {
          suspended_binds_.push_back({sc.cvar, GetSetting(sc.cvar)});
          rex::cvar::SetFlagByName(sc.cvar, "");
        }
      }
      const std::string bound = capturing_;
      Capture(io, cancel);
      if (capturing_.empty()) {
        for (const auto& [cvar, value] : suspended_binds_) {
          if (cvar != bound || (captured_shortcut_key_.empty() && cancel)) {
            rex::cvar::SetFlagByName(cvar, value);
          }
        }
        suspended_binds_.clear();
        if (!captured_shortcut_key_.empty()) {
          for (const auto& sc : ShortcutList()) {
            if (sc.cvar != bound && GetSetting(sc.cvar) == captured_shortcut_key_) Set(sc.cvar, "");
          }
          captured_shortcut_key_.clear();
        }
      }
    } else if (pages_[page_].controller) {
      ControllerFrame(io);
    } else if (pages_[page_].jutsu) {
      JutsuFrame(io);
    } else if (pages_[page_].grid) {
      GridFrame(io);
    } else {
      HandleInput(ReadInput(io));
      if (close_time_ < 0.0) HandleMouse(io);
    }
  }

  Draw(io);

  if (dirty_ && capturing_.empty()) {
    Save();
  }
  if (close_time_ >= 0.0 && time_ - close_time_ > 0.22) {
    Close();
  }
}

void OptionsMenu::Draw(ImGuiIO& io) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float w = io.DisplaySize.x;
  const float h = io.DisplaySize.y;

  // Fade in when opened, out when closing.
  float alpha = EaseOut(static_cast<float>(time_ / 0.25));
  if (close_time_ >= 0.0) {
    alpha *= 1.0f - EaseOut(static_cast<float>((time_ - close_time_) / 0.2));
  }

  // Darken the game like its own Options screen, a little more on the left behind the text.
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, static_cast<int>(150 * alpha)));
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w * 0.7f, h),
                              IM_COL32(0, 0, 10, static_cast<int>(150 * alpha)),
                              IM_COL32(0, 0, 10, 0), IM_COL32(0, 0, 10, 0),
                              IM_COL32(0, 0, 10, static_cast<int>(150 * alpha)));

  Page& page = pages_[page_];
  const float page_t = static_cast<float>(time_ - page_time_);
  const float x0 = S(150.0f);
  const float title_y = S(220.0f);

  // Title slides in from the left on every page change; the root title sits above subpages.
  {
    const float slide = (1.0f - EaseOut(page_t / 0.25f)) * S(-70.0f);
    if (!page_stack_.empty()) {
      OutlinedText(dl, ImVec2(x0 + slide, title_y - S(44.0f)), S(32.0f), kBlue, "PC OPTIONS",
                   alpha);
    }
    OutlinedText(dl, ImVec2(x0 + slide, title_y), S(62.0f), kWhite, page.title, alpha, 3.0f);
  }

  if (page.controller) {
    DrawController(dl, io, alpha);
    return;
  }
  if (page.jutsu) {
    DrawJutsu(dl, io, alpha);
    return;
  }
  if (page.grid) {
    DrawGrid(dl, io, alpha);
    return;
  }

  // Rows.
  const int count = static_cast<int>(page.items.size());
  if (page.selected < page.scroll) page.scroll = page.selected;
  if (page.selected >= page.scroll + kVisibleRows) page.scroll = page.selected - kVisibleRows + 1;
  const float row_y0 = title_y + S(96.0f);
  const float row_h = page.tall ? S(150.0f) : S(56.0f);
  const float label_x = x0 + S(56.0f);
  const float value_x = x0 + S(760.0f);
  const float item_size = S(44.0f);
  hover_row_ = -1;

  for (int row = 0; row < kVisibleRows && page.scroll + row < count; ++row) {
    const int index = page.scroll + row;
    Item& item = page.items[index];
    const bool selected = index == page.selected;

    // Items slide in one after another.
    const float delay = row * 0.03f;
    const float t = EaseOut((page_t - delay) / 0.22f);
    const float a = alpha * t;
    const float slide = (1.0f - t) * S(-60.0f);
    // Exit Game sits a little apart at the bottom.
    const float y = row_y0 + row * row_h + (item.exit ? S(28.0f) : 0.0f);

    if (io.MousePos.y >= y && io.MousePos.y < y + row_h && io.MousePos.x >= x0 &&
        io.MousePos.x < value_x + S(560.0f)) {
      hover_row_ = index;
    }

    // The selected row grows a little and its arrow bobs, like the game's highlight.
    const float grow = selected ? EaseOut(static_cast<float>((time_ - select_time_) / 0.12)) : 0.0f;
    const float size = item_size * (1.0f + 0.05f * grow);
    const float text_y = y + (row_h - size) * 0.5f;
    const ImU32 color = selected ? kOrange : kBlue;
    if (selected) {
      const float bob = std::sin(static_cast<float>(time_) * 7.0f) * S(4.0f);
      Arrow(dl, ImVec2(x0 + S(18.0f) + slide + bob, y + row_h * 0.5f), S(26.0f), kOrange, a);
    }
    std::string label = item.label;
    if (item.exit && exit_armed_ && selected) {
      label = "EXIT GAME?  PRESS AGAIN TO CONFIRM";
    }
    float text_x = label_x + slide;
    if (item.picture >= 0 && item.picture < static_cast<int>(pictures_.size()) &&
        pictures_[item.picture]) {
      // The portrait, a little larger while selected.
      const float pic = S(130.0f) * (1.0f + 0.06f * grow);
      const float py = y + (row_h - pic) * 0.5f;
      dl->AddImage(reinterpret_cast<ImTextureID>(pictures_[item.picture].get()), ImVec2(text_x, py),
                   ImVec2(text_x + pic, py + pic), ImVec2(0, 0), ImVec2(1, 1),
                   WithAlpha(IM_COL32(255, 255, 255, 255), a));
      text_x += S(160.0f);
    }
    OutlinedText(dl, ImVec2(text_x, text_y), size, color, label, a);

    // Value column.
    const float value_size = S(40.0f);
    const float vy = y + (row_h - value_size) * 0.5f;
    if (item.kind == Item::Kind::kChoice && item.choices.size() == 2) {
      // Two-way settings show both answers, like the game's NO / YES.
      const int current = ChoiceIndex(item);
      float vx = value_x + slide;
      for (int c = 0; c < 2; ++c) {
        const bool on = c == current;
        ImU32 vc = on ? (selected ? kOrange : kWhite) : kBlue;
        const float va = on ? a : a * 0.55f;
        OutlinedText(dl, ImVec2(vx, vy), value_size, vc, item.choices[c].label, va);
        vx += TextSize(value_size, item.choices[c].label).x + S(48.0f);
      }
    } else if (item.kind == Item::Kind::kSlider) {
      // The game's orange volume bar.
      const float value = static_cast<float>(std::atof(GetSetting(item.cvar).c_str()));
      const float fill = (value - item.min) / (item.max - item.min);
      const float bx = value_x + slide;
      const float bw = S(320.0f);
      const float bh = S(16.0f);
      const float by = y + (row_h - bh) * 0.5f;
      dl->AddRectFilled(ImVec2(bx - S(3), by - S(3)), ImVec2(bx + bw + S(3), by + bh + S(3)),
                        WithAlpha(kOutline, a), S(4));
      dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), WithAlpha(IM_COL32(40, 40, 60, 255), a),
                        S(3));
      dl->AddRectFilledMultiColor(ImVec2(bx, by), ImVec2(bx + bw * fill, by + bh),
                                  WithAlpha(IM_COL32(255, 196, 80, 255), a),
                                  WithAlpha(IM_COL32(255, 196, 80, 255), a),
                                  WithAlpha(IM_COL32(240, 120, 10, 255), a),
                                  WithAlpha(IM_COL32(240, 120, 10, 255), a));
      OutlinedText(dl, ImVec2(bx + bw + S(24.0f), vy), value_size, selected ? kOrange : kWhite,
                   ValueText(item), a);
    } else if (item.kind == Item::Kind::kKeyBind) {
      const bool capturing = capturing_ == item.cvar;
      if (capturing) {
        const float blink = 0.55f + 0.45f * std::sin(static_cast<float>(time_) * 8.0f);
        OutlinedText(dl, ImVec2(value_x + slide, vy), value_size, kOrange,
                     append_key_ ? "PRESS A SECOND KEY..." : "PRESS A KEY...", a * blink);
      } else {
        const std::string keys = ValueText(item);
        OutlinedText(dl, ImVec2(value_x + slide, vy), value_size, selected ? kOrange : kWhite,
                     keys, a);
        if (!item.mode_cvar.empty()) {
          const std::string mode = GetSetting(item.mode_cvar);
          if (mode != "normal" || selected) {
            const float kx = value_x + slide + TextSize(value_size, keys).x + S(36.0f);
            OutlinedText(dl, ImVec2(kx, vy + S(6.0f)), S(30.0f),
                         mode != "normal" ? kOrange : kBlue, ModeLabel(mode), a);
          }
        }
      }
    } else if ((item.kind == Item::Kind::kSubmenu || item.kind == Item::Kind::kAction) &&
               item.summary) {
      OutlinedText(dl, ImVec2(value_x + slide, vy + S(4.0f)), S(34.0f),
                   selected ? kOrange : kWhite, item.summary(), a);
    } else if (item.kind == Item::Kind::kChoice) {
      const std::string text = ValueText(item);
      const float tw = TextSize(value_size, text).x;
      float vx = value_x + slide;
      if (selected) {
        Arrow(dl, ImVec2(vx + S(10.0f), vy + value_size * 0.5f), S(18.0f), kOrange, a, true);
        vx += S(36.0f);
      }
      const float pulse = selected ? flash_ : 0.0f;
      const ImU32 vc = selected ? kOrange : kWhite;
      OutlinedText(dl, ImVec2(vx, vy - pulse * S(3.0f)), value_size * (1.0f + 0.08f * pulse), vc,
                   text, a);
      if (selected) {
        Arrow(dl, ImVec2(vx + tw + S(26.0f), vy + value_size * 0.5f), S(18.0f), kOrange, a);
      }
    }
  }

  // Scroll hints for long pages.
  if (page.scroll > 0) {
    OutlinedText(dl, ImVec2(label_x, row_y0 - S(34.0f)), S(28.0f), kBlue, "...", alpha);
  }
  if (page.scroll + kVisibleRows < count) {
    OutlinedText(dl, ImVec2(label_x, row_y0 + kVisibleRows * row_h - S(6.0f)), S(28.0f), kBlue,
                 "...", alpha);
  }

  // Help line for the selected item, and the restart notice.
  float bottom_y = h - S(110.0f);
  if (count > 0 && !page.items[page.selected].help.empty()) {
    std::string help = page.items[page.selected].help;
    const auto token = help.find("{fps_key}");
    if (token != std::string::npos) {
      const std::string key = GetSetting("bind_fps_counter");
      help.replace(token, 9, key.empty() ? "its key (none set)" : key);
    }
    OutlinedText(dl, ImVec2(x0, bottom_y), S(28.0f), kWhite, Upper(help), alpha * 0.9f);
  }
  if (restart_needed_) {
    OutlinedText(dl, ImVec2(x0, bottom_y + S(40.0f)), S(26.0f), kOrange,
                 "SOME CHANGES APPLY AFTER RESTARTING THE GAME", alpha);
  }

  // Button prompts, bottom right like the game.
  const float prompt_y = h - S(80.0f);
  float right = w - S(110.0f);
  const Item* current = count > 0 ? &page.items[page.selected] : nullptr;
  right = Prompt(dl, right, prompt_y, "BACK", 'B', "ESC", pad_prompts_, alpha);
  if (current && current->kind == Item::Kind::kKeyBind) {
    if (!current->single_key) {
      right = Prompt(dl, right, prompt_y, "ADD KEY", 'Y', "INS", pad_prompts_, alpha);
    }
    right = Prompt(dl, right, prompt_y, "CLEAR", 'X', "DEL", pad_prompts_, alpha);
    if (!current->mode_cvar.empty()) {
      right = Prompt(dl, right, prompt_y, "MODE", ' ', pad_prompts_ ? "D-PAD" : "LEFT/RIGHT",
                     false, alpha);
    }
    right = Prompt(dl, right, prompt_y, "CHANGE", 'A', "ENTER", pad_prompts_, alpha);
  } else if (current && current->kind == Item::Kind::kSubmenu) {
    right = Prompt(dl, right, prompt_y, "SELECT", 'A', "ENTER", pad_prompts_, alpha);
  } else if (current && current->kind == Item::Kind::kAction) {
    right = Prompt(dl, right, prompt_y, "CONFIRM", 'A', "ENTER", pad_prompts_, alpha);
  } else {
    right = Prompt(dl, right, prompt_y, "CHANGE", 'A', "ENTER", pad_prompts_, alpha);
  }
}

// ============================================================== Controller layout page

namespace {

// Where each button sits on the drawn controller, relative to its center, in 1080p pixels.
enum class Shape { kCircle, kRect };
struct Hotspot {
  float x, y;    // center
  Shape shape;
  float w, h;    // circle: radius in w; rect: half extents
};
constexpr std::array<Hotspot, layout::kButtonCount> kHotspots = {{
    {190, 2, Shape::kCircle, 25, 0},         // A
    {245, -55, Shape::kCircle, 25, 0},       // B
    {135, -55, Shape::kCircle, 25, 0},       // X
    {190, -112, Shape::kCircle, 25, 0},      // Y
    {-210, -178, Shape::kRect, 78, 17},      // LB
    {210, -178, Shape::kRect, 78, 17},       // RB
    {-215, -240, Shape::kRect, 50, 28},      // LT
    {215, -240, Shape::kRect, 50, 28},       // RT
    {-190, -55, Shape::kCircle, 50, 0},      // LS
    {95, 55, Shape::kCircle, 46, 0},         // RS
    {-62, -62, Shape::kCircle, 16, 0},       // Back
    {62, -62, Shape::kCircle, 16, 0},        // Start
    {-95, 22, Shape::kRect, 15, 20},         // D-pad up
    {-95, 88, Shape::kRect, 15, 20},         // D-pad down
    {-128, 55, Shape::kRect, 20, 15},        // D-pad left
    {-62, 55, Shape::kRect, 20, 15},         // D-pad right
}};

// Badges sit in two columns outside the controller, each in its own slot, joined to their
// button by a line, so they never cover another button.
// Back and Start badges sit above the controller instead of in a side column.
bool OnTop(int b) { return b == layout::kBack || b == layout::kStart; }
constexpr float kTopSlotY = -215.0f;

// Badge color: A, B, X and Y use their button's color, the rest orange.
ImU32 BadgeColor(int b) {
  static const ImU32 kFace[4] = {IM_COL32(70, 170, 50, 255), IM_COL32(210, 45, 45, 255),
                                 IM_COL32(40, 110, 230, 255), IM_COL32(235, 185, 20, 255)};
  return b <= layout::kY ? kFace[b] : IM_COL32(255, 140, 24, 255);
}

struct Callout {
  bool right;  // right column, else left
  float y;     // slot height, relative to the controller center
};
constexpr Callout kCallouts[layout::kButtonCount] = {
    {true, 15},     // A
    {true, -55},    // B
    {true, -20},    // X
    {true, -125},   // Y
    {false, -180},  // LB
    {true, -180},   // RB
    {false, -240},  // LT
    {true, -240},   // RT
    {false, -55},   // LS
    {true, 60},     // RS
    {false, -90},   // Back
    {true, -90},    // Start
    {false, -20},   // D-pad up
    {false, 85},    // D-pad down
    {false, 15},    // D-pad left
    {false, 50},    // D-pad right
};

constexpr std::array<const char*, layout::kButtonCount> kShortNames = {
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "BACK", "START",
    "UP", "DOWN", "LEFT", "RIGHT"};

int PadTarget(int button) {
  const std::string value = GetSetting(PadCvar("map", button));
  for (int t = 0; t < layout::kButtonCount; ++t) {
    if (value == layout::kNames[t]) return t;
  }
  return layout::kNone;
}

std::string PadMode(int button) {
  return GetSetting(PadCvar("mode", button));
}

std::string TargetLabel(int target) {
  return target == layout::kNone ? "NOTHING" : kButtonLabels[target];
}

// Nearest button in a direction, preferring ones straight ahead.
int NeighborInDirection(int from, float dx, float dy) {
  int best = from;
  float best_score = 1e9f;
  for (int i = 0; i < layout::kButtonCount; ++i) {
    if (i == from) continue;
    const float vx = kHotspots[i].x - kHotspots[from].x;
    const float vy = kHotspots[i].y - kHotspots[from].y;
    const float along = vx * dx + vy * dy;
    if (along <= 1.0f) continue;
    const float across = std::fabs(vx * dy - vy * dx);
    const float score = along + across * 2.5f;
    if (score < best_score) {
      best_score = score;
      best = i;
    }
  }
  return best;
}

}  // namespace

void OptionsMenu::SetPadTarget(int button, int target) {
  Set(PadCvar("map", button),
      target == layout::kNone ? "none" : layout::kNames[target]);
  pad_changed_time_ = time_;
}

void OptionsMenu::ControllerFrame(ImGuiIO& io) {
  if (pad_capturing_) {
    // Waiting for the button the selected one should act as. Only the controller and the
    // keyboard keys below count, so a D-pad press is a target here, not navigation.
    const PadRaw raw = ReadPadRawAt(g_pad_layout_index, nullptr);
    const bool any = (raw.buttons & layout::kMappableBits) != 0 ||
                     raw.left_trigger >= layout::kTriggerThreshold ||
                     raw.right_trigger >= layout::kTriggerThreshold;
    const int count = layout::kButtonCount + 1;  // the last choice is "nothing"
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      pad_capturing_ = false;
    } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
      pad_capture_choice_ = (pad_capture_choice_ + count - 1) % count;
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
      pad_capture_choice_ = (pad_capture_choice_ + 1) % count;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
               ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
      SetPadTarget(pad_selected_,
                   pad_capture_choice_ == layout::kButtonCount ? layout::kNone : pad_capture_choice_);
      pad_capturing_ = false;
    } else if (!pad_capture_ready_) {
      pad_capture_ready_ = !any;  // wait until the A that started this is released
    } else if (any) {
      layout::PadState state{raw.buttons, raw.left_trigger, raw.right_trigger};
      for (int t = 0; t < layout::kButtonCount; ++t) {
        if (layout::IsPressed(state, t)) {
          SetPadTarget(pad_selected_, t);
          break;
        }
      }
      pad_capturing_ = false;
      pad_prompts_ = true;
    }
    if (!pad_capturing_) {
      pad_held_ = ReadPads();  // the button just pressed is not a new menu press
    }
    return;
  }

  const Nav nav = ReadInput(io);
  const int before = pad_selected_;
  if (nav.up) pad_selected_ = NeighborInDirection(pad_selected_, 0, -1);
  if (nav.down) pad_selected_ = NeighborInDirection(pad_selected_, 0, 1);
  if (nav.left) pad_selected_ = NeighborInDirection(pad_selected_, -1, 0);
  if (nav.right) pad_selected_ = NeighborInDirection(pad_selected_, 1, 0);

  // Mouse: hover picks a button, click remaps it, right click goes back.
  const bool moved = io.MousePos.x != last_mouse_x_ || io.MousePos.y != last_mouse_y_;
  last_mouse_x_ = io.MousePos.x;
  last_mouse_y_ = io.MousePos.y;
  if (pad_hover_ >= 0 && moved) pad_selected_ = pad_hover_;
  const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && pad_hover_ >= 0;

  if (pad_selected_ != before) {
    select_time_ = time_;
    pad_reset_armed_ = false;
  }

  if (nav.back || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    pad_reset_armed_ = false;
    page_ = page_stack_.back();
    page_stack_.pop_back();
    page_time_ = time_;
    select_time_ = time_;
    return;
  }
  if (nav.confirm || click) {
    pad_capturing_ = true;
    pad_capture_ready_ = false;
    const int target = PadTarget(pad_selected_);
    pad_capture_choice_ = target == layout::kNone ? layout::kButtonCount : target;
    pad_reset_armed_ = false;
  }
  if (nav.add) {
    // Y: cycle normal / toggle / turbo.
    const std::string mode = PadMode(pad_selected_);
    size_t index = 0;
    for (size_t i = 0; i < kModes.size(); ++i) {
      if (kModes[i].first == mode) index = i;
    }
    Set(PadCvar("mode", pad_selected_),
        kModes[(index + 1) % kModes.size()].first);
    pad_changed_time_ = time_;
  }
  if (nav.clear) {
    // X: disable the button, or give it back its own function.
    SetPadTarget(pad_selected_, PadTarget(pad_selected_) == layout::kNone ? pad_selected_
                                                                         : layout::kNone);
  }
  if (nav.start || ImGui::IsKeyPressed(ImGuiKey_R, false)) {
    if (!pad_reset_armed_) {
      pad_reset_armed_ = true;
    } else {
      for (int b = 0; b < layout::kButtonCount; ++b) {
        SetPadTarget(b, b);
        Set(PadCvar("mode", b), "normal");
      }
      pad_reset_armed_ = false;
    }
  }
}

void OptionsMenu::DrawController(ImDrawList* dl, ImGuiIO& io, float alpha) {
  const float w = io.DisplaySize.x;
  const float h = io.DisplaySize.y;
  const float page_t = static_cast<float>(time_ - page_time_);
  const float appear = EaseOut(page_t / 0.3f);
  const float a = alpha * appear;
  const float k = S(1.0f) * (0.92f + 0.08f * appear);  // grows into place
  const float cx = w * 0.5f + S(60.0f);
  const float cy = S(560.0f);
  auto P = [&](float x, float y) { return ImVec2(cx + x * k, cy + y * k); };

  const ImU32 body = WithAlpha(IM_COL32(26, 32, 58, 255), a);
  const ImU32 line = WithAlpha(IM_COL32(235, 240, 255, 255), a * 0.9f);
  const ImU32 part = WithAlpha(IM_COL32(52, 60, 92, 255), a);
  const ImU32 part_hi = WithAlpha(IM_COL32(78, 88, 128, 255), a);

  // Triggers and bumpers sit behind the body.
  for (int b : {layout::kLT, layout::kRT, layout::kLB, layout::kRB}) {
    const Hotspot& hs = kHotspots[b];
    const ImVec2 p0 = P(hs.x - hs.w, hs.y - hs.h);
    const ImVec2 p1 = P(hs.x + hs.w, hs.y + hs.h);
    dl->AddRectFilled(ImVec2(p0.x - 3, p0.y - 3), ImVec2(p1.x + 3, p1.y + 3), line, S(14) * k);
    dl->AddRectFilled(p0, p1, part, S(12) * k);
  }

  // Body: the outline pass draws every shape slightly larger, then the fill pass covers it.
  auto body_shapes = [&](float grow, ImU32 color) {
    const float g = grow;
    dl->AddRectFilled(ImVec2(P(-300, -152).x - g, P(-300, -152).y - g),
                      ImVec2(P(300, 40).x + g, P(300, 40).y + g), color, 100.0f * k + g);
    dl->AddRectFilled(ImVec2(P(-175, -60).x - g, P(-175, -60).y - g),
                      ImVec2(P(175, 100).x + g, P(175, 100).y + g), color, 55.0f * k + g);
    dl->AddEllipseFilled(P(-218, 100), ImVec2(105.0f * k + g, 145.0f * k + g), color, 0.42f);
    dl->AddEllipseFilled(P(218, 100), ImVec2(105.0f * k + g, 145.0f * k + g), color, -0.42f);
  };
  body_shapes(S(4.0f), line);
  body_shapes(0.0f, body);

  // Guide button, decorative.
  dl->AddCircleFilled(P(0, -78), 30.0f * k, part);
  dl->AddCircle(P(0, -78), 30.0f * k, line, 0, S(2.0f));

  // D-pad cross.
  const float o = S(3.0f);
  dl->AddRectFilled(ImVec2(P(-110, 5).x - o, P(-110, 5).y - o),
                    ImVec2(P(-80, 105).x + o, P(-80, 105).y + o), line, 7.0f * k);
  dl->AddRectFilled(ImVec2(P(-145, 40).x - o, P(-145, 40).y - o),
                    ImVec2(P(-45, 70).x + o, P(-45, 70).y + o), line, 7.0f * k);
  dl->AddRectFilled(P(-110, 5), P(-80, 105), part_hi, 6.0f * k);
  dl->AddRectFilled(P(-145, 40), P(-45, 70), part_hi, 6.0f * k);

  const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(time_) * 6.0f);
  const ImU32 face_colors[4] = {IM_COL32(70, 170, 50, 255), IM_COL32(210, 45, 45, 255),
                                IM_COL32(40, 110, 230, 255), IM_COL32(235, 185, 20, 255)};

  // Leader lines for changed buttons, under everything else.
  for (int b = 0; b < layout::kButtonCount; ++b) {
    if (PadTarget(b) == b && PadMode(b) == "normal") continue;
    const Hotspot& hs = kHotspots[b];
    const Callout& co = kCallouts[b];
    const ImVec2 c = P(hs.x, hs.y);
    const float edge = hs.w * k;
    ImVec2 slot = P(co.right ? 335.0f : -335.0f, co.y);
    ImVec2 from(c.x + (co.right ? edge : -edge), c.y);
    if (OnTop(b)) {
      slot = P(hs.x, kTopSlotY);
      from = ImVec2(c.x, c.y - edge);
    }
    dl->AddLine(from, slot, WithAlpha(kOutline, a), S(5.0f));
    dl->AddLine(from, slot, WithAlpha(BadgeColor(b), a), S(2.5f));
  }

  pad_hover_ = -1;
  for (int b = 0; b < layout::kButtonCount; ++b) {
    const Hotspot& hs = kHotspots[b];
    const bool selected = b == pad_selected_;
    const ImVec2 c = P(hs.x, hs.y);
    const float hit = (hs.shape == Shape::kCircle ? hs.w : std::max(hs.w, hs.h)) * k + S(8.0f);
    if (std::fabs(io.MousePos.x - c.x) < hit && std::fabs(io.MousePos.y - c.y) < hit) {
      pad_hover_ = b;
    }

    // The button itself.
    if (hs.shape == Shape::kCircle) {
      const float r = hs.w * k;
      ImU32 fill = part;
      if (b <= layout::kY) fill = WithAlpha(face_colors[b], a * 0.9f);
      if (b == layout::kLS || b == layout::kRS) {
        dl->AddCircleFilled(c, r, part);
        dl->AddCircleFilled(c, r * 0.66f, part_hi);
      } else {
        dl->AddCircleFilled(c, r, fill);
      }
      dl->AddCircle(c, r, line, 0, S(2.0f));
    }
    const std::string label = (b == layout::kLS) ? "L" : (b == layout::kRS) ? "R" : kShortNames[b];
    const bool text_inside = b <= layout::kRT || b == layout::kLS || b == layout::kRS;
    if (text_inside) {
      const float size = (b <= layout::kY ? 30.0f : 26.0f) * k;
      const ImVec2 ts = TextSize(size, label);
      OutlinedText(dl, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), size, kWhite, label, a, 1.5f);
    } else if (b == layout::kBack || b == layout::kStart) {
      const float size = 18.0f * k;
      const ImVec2 ts = TextSize(size, label);
      OutlinedText(dl, ImVec2(c.x - ts.x * 0.5f, c.y + hs.w * k + S(4.0f)), size, kWhite, label, a,
                   1.5f);
    }

    // Highlight.
    if (selected) {
      const ImU32 glow = WithAlpha(kOrange, a * (0.6f + 0.4f * pulse));
      if (hs.shape == Shape::kCircle) {
        dl->AddCircle(c, hs.w * k + S(6.0f) + pulse * S(3.0f), glow, 0, S(5.0f));
      } else {
        const float g = S(6.0f) + pulse * S(3.0f);
        dl->AddRect(ImVec2(P(hs.x - hs.w, hs.y - hs.h).x - g, P(hs.x - hs.w, hs.y - hs.h).y - g),
                    ImVec2(P(hs.x + hs.w, hs.y + hs.h).x + g, P(hs.x + hs.w, hs.y + hs.h).y + g),
                    glow, S(8.0f), 0, S(5.0f));
        if (b >= layout::kUp) {
          dl->AddRectFilled(P(hs.x - hs.w, hs.y - hs.h), P(hs.x + hs.w, hs.y + hs.h),
                            WithAlpha(kOrange, a * 0.8f), 4.0f * k);
        }
      }
    }

    // Badge for a changed button: what it sends now, and its press mode.
    const int target = PadTarget(b);
    const std::string mode = PadMode(b);
    if (target != b || mode != "normal") {
      std::string badge = target == layout::kNone ? "OFF" : (target == b ? "" : kShortNames[target]);
      if (mode == "toggle") badge += badge.empty() ? "TOGGLE" : " TOGGLE";
      if (mode == "turbo") badge += badge.empty() ? "TURBO" : " TURBO";
      const float size = 20.0f * k;
      const ImVec2 ts = TextSize(size, badge);
      const float pill_w = ts.x + S(12.0f);
      // Badges sit in two columns outside the controller, each in its own slot, joined to
      // their button by a line, so they never cover another button.
      const Callout& co = kCallouts[b];
      const float column_x = (co.right ? 335.0f : -335.0f);
      ImVec2 slot = P(column_x, co.y);
      float bx = co.right ? slot.x : slot.x - pill_w;
      float by = slot.y - ts.y * 0.5f;
      const float edge = hs.w * k;
      ImVec2 dot(c.x + (co.right ? edge : -edge), c.y);
      if (OnTop(b)) {
        slot = P(hs.x, kTopSlotY);
        bx = slot.x - pill_w * 0.5f;
        by = slot.y - ts.y - S(3.0f);
        dot = ImVec2(c.x, c.y - edge);
      }
      const ImU32 badge_color = BadgeColor(b);
      dl->AddCircleFilled(dot, S(4.0f), WithAlpha(badge_color, a));
      const float just = std::clamp(static_cast<float>(time_ - pad_changed_time_) / 0.3f, 0.0f, 1.0f);
      const float grow = (b == pad_selected_) ? (1.0f - just) * S(4.0f) : 0.0f;
      dl->AddRectFilled(ImVec2(bx - grow, by - S(3.0f) - grow),
                        ImVec2(bx + ts.x + S(12.0f) + grow, by + ts.y + S(3.0f) + grow),
                        WithAlpha(badge_color, a), S(8.0f));
      const ImU32 badge_text = (b <= layout::kX) ? kWhite : kOutline;  // dark text on yellow/orange
      dl->AddText(Font(), size, ImVec2(bx + S(6.0f), by), WithAlpha(badge_text, a), badge.c_str());
    }
  }

  // Information about the selected button, under the controller.
  const float info_y = cy + 285.0f * k;
  const std::string name = kButtonLabels[pad_selected_];
  const ImVec2 ns = TextSize(S(46.0f), name);
  OutlinedText(dl, ImVec2(cx - ns.x * 0.5f, info_y), S(46.0f), kOrange, name, a, 3.0f);
  std::string line2;
  ImU32 line2_color = kWhite;
  if (pad_capturing_) {
    const float blink = 0.55f + 0.45f * std::sin(static_cast<float>(time_) * 8.0f);
    line2 = "PRESS THE BUTTON IT SHOULD ACT AS";
    const ImVec2 ls = TextSize(S(34.0f), line2);
    OutlinedText(dl, ImVec2(cx - ls.x * 0.5f, info_y + S(56.0f)), S(34.0f), kOrange, line2,
                 a * blink);
    const std::string hint =
        "OR LEFT / RIGHT TO CHOOSE (" +
        TargetLabel(pad_capture_choice_ == layout::kButtonCount ? layout::kNone
                                                                : pad_capture_choice_) +
        "), ENTER TO CONFIRM, ESC TO CANCEL";
    const ImVec2 hs2 = TextSize(S(24.0f), hint);
    OutlinedText(dl, ImVec2(cx - hs2.x * 0.5f, info_y + S(100.0f)), S(24.0f), kBlue, hint, a);
  } else {
    const int target = PadTarget(pad_selected_);
    line2 = "SENDS  " + TargetLabel(target) + "      MODE  " + ModeLabel(PadMode(pad_selected_));
    const ImVec2 ls = TextSize(S(32.0f), line2);
    OutlinedText(dl, ImVec2(cx - ls.x * 0.5f, info_y + S(56.0f)), S(32.0f), line2_color, line2, a);
    if (pad_reset_armed_) {
      const std::string warn = "PRESS " + std::string(pad_prompts_ ? "START" : "R") +
                               " AGAIN TO RESET EVERY BUTTON";
      const ImVec2 ws = TextSize(S(28.0f), warn);
      OutlinedText(dl, ImVec2(cx - ws.x * 0.5f, info_y + S(100.0f)), S(28.0f), kOrange, warn, a);
    }
  }

  // Prompts.
  const float prompt_y = h - S(80.0f);
  float right = w - S(110.0f);
  if (pad_capturing_) {
    right = Prompt(dl, right, prompt_y, "CANCEL", ' ', "ESC", false, alpha);
    return;
  }
  right = Prompt(dl, right, prompt_y, "BACK", 'B', "ESC", pad_prompts_, alpha);
  right = Prompt(dl, right, prompt_y, "RESET ALL", ' ', pad_prompts_ ? "START" : "R", false, alpha);
  right = Prompt(dl, right, prompt_y, PadTarget(pad_selected_) == layout::kNone ? "ENABLE" : "DISABLE",
                 'X', "DEL", pad_prompts_, alpha);
  right = Prompt(dl, right, prompt_y, "MODE", 'Y', "INS", pad_prompts_, alpha);
  right = Prompt(dl, right, prompt_y, "REMAP", 'A', "ENTER", pad_prompts_, alpha);
}

// ============================================================== Auto Jutsu page

namespace {

constexpr int kJutsuToggleRow = 0;
constexpr int kJutsuFirstRow = 1;  // jutsu 1-4 are rows 1-4
constexpr int kJutsuResetRow = 5;
constexpr int kJutsuColumns = 3;   // keyboard, controller 1, controller 2

bool JutsuOn() { return GetSetting("auto_jutsu") == "true"; }

// The setting behind a cell, empty for the rows that have none.
std::string JutsuCvar(int row, int col) {
  if (row < kJutsuFirstRow || row >= kJutsuResetRow) return {};
  const std::string n = std::to_string(row - kJutsuFirstRow + 1);
  return col == 0 ? "auto_jutsu_key_" + n : "auto_jutsu_pad" + std::to_string(col) + "_" + n;
}

std::string PadButtonText(const std::string& name) {
  static const std::array<const char*, layout::kButtonCount> kText = {
      "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LEFT STICK", "RIGHT STICK", "BACK", "START",
      "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT"};
  for (int i = 0; i < layout::kButtonCount; ++i) {
    if (name == layout::kNames[i]) return kText[i];
  }
  return "NONE";
}

}  // namespace

void OptionsMenu::ActivateJutsuCell() {
  if (jutsu_row_ == kJutsuToggleRow) {
    Set("auto_jutsu", JutsuOn() ? "false" : "true");
    return;
  }
  if (jutsu_row_ == kJutsuResetRow) {
    for (int i = 1; i <= 4; ++i) {
      Set("auto_jutsu_key_" + std::to_string(i), "Numpad" + std::to_string(i));
    }
    for (int pad = 1; pad <= 2; ++pad) {
      const std::string prefix = "auto_jutsu_pad" + std::to_string(pad) + "_";
      Set(prefix + "1", "a");
      Set(prefix + "2", "b");
      Set(prefix + "3", "y");
      Set(prefix + "4", "x");
    }
    jutsu_reset_time_ = time_;
    return;
  }
  const std::string cvar = JutsuCvar(jutsu_row_, jutsu_col_);
  if (cvar.empty()) return;
  if (jutsu_col_ == 0) {
    capturing_ = cvar;  // the next key pressed, through the menu's key capture
    append_key_ = false;
    capture_frames_ = 0;
  } else {
    jutsu_pad_capturing_ = true;
    jutsu_pad_ready_ = false;
  }
}

void OptionsMenu::JutsuFrame(ImGuiIO& io) {
  // Binding a controller button: the next button pressed on that controller.
  if (jutsu_pad_capturing_) {
    bool connected = false;
    const PadRaw raw = ReadPadRawAt(jutsu_col_ - 1, &connected);
    const bool any = raw.buttons != 0 || raw.left_trigger > 60 || raw.right_trigger > 60;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      jutsu_pad_capturing_ = false;
    } else if (!jutsu_pad_ready_) {
      jutsu_pad_ready_ = connected && !any;  // wait until the press that started this is let go
    } else if (any) {
      int pressed = layout::kNone;
      if (raw.right_trigger > 60) pressed = layout::kRT;
      if (raw.left_trigger > 60) pressed = layout::kLT;
      for (int b = 0; b < layout::kButtonCount && pressed == layout::kNone; ++b) {
        if (layout::kBits[b] && (raw.buttons & layout::kBits[b])) pressed = b;
      }
      if (pressed != layout::kNone) {
        Set(JutsuCvar(jutsu_row_, jutsu_col_), layout::kNames[pressed]);
        jutsu_pad_capturing_ = false;
        pad_prompts_ = true;
      }
    }
    if (!jutsu_pad_capturing_) {
      pad_held_ = ReadPads();  // the button just bound is not a menu press
    }
    return;
  }

  const Nav nav = ReadInput(io);
  const bool on = JutsuOn();
  const int before_row = jutsu_row_;
  const int before_col = jutsu_col_;

  if (nav.back || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    page_ = page_stack_.back();
    page_stack_.pop_back();
    page_time_ = time_;
    select_time_ = time_;
    return;
  }
  if (on) {
    // No wrap-around: holding down stops on Reset instead of reaching the on / off switch.
    if (nav.up) jutsu_row_ = std::max(jutsu_row_ - 1, kJutsuToggleRow);
    if (nav.down) jutsu_row_ = std::min(jutsu_row_ + 1, kJutsuResetRow);
  } else {
    jutsu_row_ = kJutsuToggleRow;
  }
  const bool grid_row = jutsu_row_ >= kJutsuFirstRow && jutsu_row_ < kJutsuResetRow;
  if (grid_row) {
    if (nav.left) jutsu_col_ = (jutsu_col_ + kJutsuColumns - 1) % kJutsuColumns;
    if (nav.right) jutsu_col_ = (jutsu_col_ + 1) % kJutsuColumns;
  } else if (jutsu_row_ == kJutsuToggleRow && (nav.left || nav.right)) {
    ActivateJutsuCell();
  }

  // Mouse: hover picks a cell, click changes it.
  const bool moved = io.MousePos.x != last_mouse_x_ || io.MousePos.y != last_mouse_y_;
  last_mouse_x_ = io.MousePos.x;
  last_mouse_y_ = io.MousePos.y;
  if (moved && jutsu_hover_row_ >= 0) {
    jutsu_row_ = jutsu_hover_row_;
    if (jutsu_hover_col_ >= 0) jutsu_col_ = jutsu_hover_col_;
  }
  const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && jutsu_hover_row_ >= 0;

  if (jutsu_row_ != before_row || jutsu_col_ != before_col) select_time_ = time_;
  if (nav.confirm || click) ActivateJutsuCell();
  if (nav.clear) {
    const std::string cvar = JutsuCvar(jutsu_row_, jutsu_col_);
    if (!cvar.empty()) Set(cvar, "");
  }
}

void OptionsMenu::DrawJutsu(ImDrawList* dl, ImGuiIO& io, float alpha) {
  const float w = io.DisplaySize.x;
  const float h = io.DisplaySize.y;
  const float page_t = static_cast<float>(time_ - page_time_);
  const float x0 = S(150.0f);
  const float label_x = x0 + S(56.0f);
  const float row_h = S(56.0f);
  const float y0 = S(220.0f) + S(96.0f);
  const bool on = JutsuOn();
  jutsu_hover_row_ = -1;
  jutsu_hover_col_ = -1;

  auto in_rect = [&](float x, float y, float rw, float rh) {
    return io.MousePos.x >= x && io.MousePos.x < x + rw && io.MousePos.y >= y &&
           io.MousePos.y < y + rh;
  };
  // Rows slide in one after another, like the other pages.
  auto fade_alpha = [&](int order) {
    return alpha * EaseOut((page_t - order * 0.03f) / 0.22f);
  };
  auto fade_slide = [&](int order) {
    return (1.0f - EaseOut((page_t - order * 0.03f) / 0.22f)) * S(-60.0f);
  };
  auto row_label = [&](int row, float y, const std::string& text, int order) {
    const float a = fade_alpha(order);
    const float slide = fade_slide(order);
    const bool selected = jutsu_row_ == row;
    if (selected && (row == kJutsuToggleRow || row == kJutsuResetRow)) {
      const float bob = std::sin(static_cast<float>(time_) * 7.0f) * S(4.0f);
      Arrow(dl, ImVec2(x0 + S(18.0f) + slide + bob, y + row_h * 0.5f), S(26.0f), kOrange, a);
    }
    const float size = S(40.0f);
    OutlinedText(dl, ImVec2(label_x + slide, y + (row_h - size) * 0.5f), size,
                 selected ? kOrange : kBlue, text, a);
  };

  // On / off, both answers shown like the game's NO / YES.
  {
    row_label(kJutsuToggleRow, y0, "AUTO JUTSU", 0);
    const float a = fade_alpha(0);
    const float value_size = S(40.0f);
    float vx = x0 + S(760.0f) + fade_slide(0);
    const float vy = y0 + (row_h - value_size) * 0.5f;
    for (int c = 0; c < 2; ++c) {
      const bool current = (c == 1) == on;
      const std::string text = c == 0 ? "OFF" : "ON";
      const ImU32 vc = current ? (jutsu_row_ == kJutsuToggleRow ? kOrange : kWhite) : kBlue;
      OutlinedText(dl, ImVec2(vx, vy), value_size, vc, text, current ? a : a * 0.55f);
      vx += TextSize(value_size, text).x + S(48.0f);
    }
    if (in_rect(x0, y0, S(1100.0f), row_h)) jutsu_hover_row_ = kJutsuToggleRow;
  }

  if (on) {
    // Column headers: the keyboard, then each controller and whether it is plugged in.
    const float col_x[kJutsuColumns] = {x0 + S(560.0f), x0 + S(960.0f), x0 + S(1360.0f)};
    const float col_w = S(380.0f);
    const float header_y = y0 + S(84.0f);
    bool connected[kJutsuColumns] = {true, false, false};
    for (int c = 1; c < kJutsuColumns; ++c) ReadPadRawAt(c - 1, &connected[c]);
    {
      const float a = fade_alpha(1);
      const float slide = fade_slide(1);
      const char* headers[kJutsuColumns] = {"KEYBOARD", "CONTROLLER 1", "CONTROLLER 2"};
      for (int c = 0; c < kJutsuColumns; ++c) {
        OutlinedText(dl, ImVec2(col_x[c] + slide, header_y), S(32.0f), kWhite, headers[c], a);
        if (!connected[c]) {
          OutlinedText(dl, ImVec2(col_x[c] + slide, header_y + S(36.0f)), S(22.0f), kBlue,
                       "NOT CONNECTED", a * 0.8f);
        }
      }
    }

    const float grid_y = header_y + S(76.0f);
    for (int row = kJutsuFirstRow; row < kJutsuResetRow; ++row) {
      const float y = grid_y + (row - kJutsuFirstRow) * row_h;
      const std::string label = "JUTSU " + std::to_string(row - kJutsuFirstRow + 1);
      row_label(row, y, label, row + 1);
      const float a = fade_alpha(row + 1);
      const float slide = fade_slide(row + 1);
      for (int c = 0; c < kJutsuColumns; ++c) {
        const std::string cvar = JutsuCvar(row, c);
        const bool selected = jutsu_row_ == row && jutsu_col_ == c;
        const float cx = col_x[c] + slide;
        const float size = S(36.0f);
        const float cy = y + (row_h - size) * 0.5f;
        if (cvar.empty()) {
          OutlinedText(dl, ImVec2(cx, cy), size, kBlue, "-", a * 0.5f);
          continue;
        }
        if (in_rect(col_x[c] - S(40.0f), y, col_w, row_h)) {
          jutsu_hover_row_ = row;
          jutsu_hover_col_ = c;
        }
        std::string text;
        float text_alpha = a;
        const bool capturing_here =
            selected && (c == 0 ? capturing_ == cvar : jutsu_pad_capturing_);
        if (capturing_here) {
          text = c == 0 ? "PRESS A KEY..." : "PRESS A BUTTON...";
          text_alpha *= 0.55f + 0.45f * std::sin(static_cast<float>(time_) * 8.0f);
        } else if (c == 0) {
          text = KeysText(GetSetting(cvar));
        } else {
          text = PadButtonText(GetSetting(cvar));
        }
        if (text.empty()) text = "NONE";
        if (selected) {
          const float bob = std::sin(static_cast<float>(time_) * 7.0f) * S(3.0f);
          Arrow(dl, ImVec2(cx - S(22.0f) + bob, y + row_h * 0.5f), S(20.0f), kOrange, a);
        }
        OutlinedText(dl, ImVec2(cx, cy), size, selected ? kOrange : kWhite, text, text_alpha);
      }
    }

    // Reset, set a little apart below the grid.
    const float reset_y = grid_y + (kJutsuResetRow - kJutsuFirstRow) * row_h + S(24.0f);
    row_label(kJutsuResetRow, reset_y, "RESET AUTO JUTSU", kJutsuResetRow + 1);
    if (time_ - jutsu_reset_time_ < 1.5) {
      OutlinedText(dl, ImVec2(x0 + S(560.0f), reset_y + (row_h - S(36.0f)) * 0.5f), S(36.0f),
                   kOrange, "DONE", alpha);
    }
    if (in_rect(x0, reset_y, S(700.0f), row_h)) jutsu_hover_row_ = kJutsuResetRow;
  }

  // Help line for the selected row.
  std::string help;
  if (jutsu_pad_capturing_) {
    help = "Press the button on controller " + std::to_string(jutsu_col_) + ". Esc cancels.";
  } else if (jutsu_row_ == kJutsuToggleRow) {
    help = "One button does your character's jutsu. Hold it to charge, let go to release.";
  } else if (jutsu_row_ == kJutsuResetRow) {
    help = "Keyboard back to Numpad 1-4, controllers back to A, B, Y, X.";
  } else {
    help = "Jutsu " + std::to_string(jutsu_row_ - kJutsuFirstRow + 1) +
           " of the jutsu menu. Controller: hold your jutsu button (LT) and press this.";
  }
  OutlinedText(dl, ImVec2(x0, h - S(110.0f)), S(28.0f), kWhite, Upper(help), alpha * 0.9f);

  // Button prompts, bottom right.
  const float prompt_y = h - S(80.0f);
  float right = w - S(110.0f);
  right = Prompt(dl, right, prompt_y, "BACK", 'B', "ESC", pad_prompts_, alpha);
  if (!JutsuCvar(jutsu_row_, jutsu_col_).empty()) {
    right = Prompt(dl, right, prompt_y, "CLEAR", 'X', "DEL", pad_prompts_, alpha);
  }
  Prompt(dl, right, prompt_y, jutsu_row_ == kJutsuResetRow ? "CONFIRM" : "CHANGE", 'A', "ENTER",
         pad_prompts_, alpha);
}

namespace {
// The item of the character grid page (14) for a cell: 0 is OFF, then the roster's order.
int GridItemOf(int cell) {
  if (cell < 0 || cell >= kGridOff || !kGridOrder[cell]) return 0;
  const auto& roster = hokage::Roster();
  for (size_t c = 0; c < roster.size(); ++c) {
    if (std::string_view(roster[c].value) == kGridOrder[cell]) return static_cast<int>(c) + 1;
  }
  return 0;
}
}  // namespace

void OptionsMenu::GridFrame(ImGuiIO& io) {
  const Nav nav = ReadInput(io);
  if (nav.back || nav.start || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    page_ = page_stack_.back();
    page_stack_.pop_back();
    page_time_ = time_;
    select_time_ = time_;
    return;
  }
  const int before = grid_cell_;
  // Arrows move over the grid; right of the last column is OFF, Naruto's cell is skipped.
  if (grid_cell_ == kGridOff) {
    if (nav.left) grid_cell_ = kGridColumns - 1;
  } else {
    int column = grid_cell_ % kGridColumns, row = grid_cell_ / kGridColumns;
    if (nav.right) {
      if (column == kGridColumns - 1) grid_cell_ = kGridOff;
      else ++column;
    }
    if (nav.left && column > 0) --column;
    if (nav.up) row = (row + kGridRows - 1) % kGridRows;
    if (nav.down) row = (row + 1) % kGridRows;
    if (grid_cell_ != kGridOff) {
      int cell = row * kGridColumns + column;
      if (!kGridOrder[cell]) cell = nav.up ? kGridOff - kGridColumns : nav.down ? kGridColumns : nav.left ? before : 1;
      grid_cell_ = cell;
    }
  }
  const bool moved = io.MousePos.x != last_mouse_x_ || io.MousePos.y != last_mouse_y_;
  last_mouse_x_ = io.MousePos.x;
  last_mouse_y_ = io.MousePos.y;
  if (grid_hover_ >= 0 && moved) grid_cell_ = grid_hover_;
  if (grid_cell_ != before) select_time_ = time_;

  const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && grid_hover_ >= 0;
  if (nav.confirm || click) {
    if (click) grid_cell_ = grid_hover_;
    Page& page = pages_[page_];
    page.selected = grid_cell_ == kGridOff ? 0 : GridItemOf(grid_cell_);
    Activate(page.items[page.selected], +1);
  }
}

void OptionsMenu::DrawGrid(ImDrawList* dl, ImGuiIO& io, float alpha) {
  const float w = io.DisplaySize.x;
  const float h = io.DisplaySize.y;
  Page& page = pages_[page_];
  const float page_t = static_cast<float>(time_ - page_time_);
  const float x0 = S(150.0f);
  const float grid_y = S(220.0f) + S(96.0f);
  const float cell = S(100.0f), pitch = S(108.0f);
  const auto* atlas = pictures_[2].get();
  const std::string mine = GetSetting(kHokageRows[picker_hokage_].second);
  const std::string other = GetSetting(kHokageRows[1 - picker_hokage_].second);
  grid_hover_ = -1;

  for (int index = 0; index < kGridOff; ++index) {
    const int column = index % kGridColumns, row = index / kGridColumns;
    // Cells pop in one after another, like the rows of the other pages.
    const float t = EaseOut((page_t - (row + column) * 0.02f) / 0.22f);
    const float a = alpha * t;
    const bool selected = index == grid_cell_;
    const bool locked = !kGridOrder[index];
    const float grow = selected ? EaseOut(static_cast<float>((time_ - select_time_) / 0.12)) : 0.0f;
    const float size = cell * (1.0f + 0.10f * grow);
    const float cx = x0 + column * pitch + cell * 0.5f, cy = grid_y + row * pitch + cell * 0.5f;
    const ImVec2 p0(cx - size * 0.5f, cy - size * 0.5f), p1(cx + size * 0.5f, cy + size * 0.5f);
    if (!locked && io.MousePos.x >= cx - cell * 0.5f && io.MousePos.x < cx + cell * 0.5f &&
        io.MousePos.y >= cy - cell * 0.5f && io.MousePos.y < cy + cell * 0.5f) {
      grid_hover_ = index;
    }

    // The game's white rounded frame, the portrait inside.
    const float frame = selected ? S(8) : S(4);
    dl->AddRectFilled(ImVec2(p0.x - frame, p0.y - frame), ImVec2(p1.x + frame, p1.y + frame),
                      WithAlpha(selected ? kOrange : IM_COL32(235, 240, 248, 255), a), S(10));
    if (atlas) {
      const ImVec2 uv0(column / float(kGridColumns), row / float(kGridRows));
      const ImVec2 uv1((column + 1) / float(kGridColumns), (row + 1) / float(kGridRows));
      dl->AddImageRounded(reinterpret_cast<ImTextureID>(atlas), p0, p1, uv0, uv1,
                          WithAlpha(locked ? IM_COL32(110, 110, 110, 255) : kWhite, a), S(5));
    } else {
      dl->AddRectFilled(p0, p1, WithAlpha(IM_COL32(60, 70, 100, 255), a), S(5));
    }
    if (locked) {
      // Naruto stays himself: crossed out.
      const ImU32 red = WithAlpha(IM_COL32(220, 30, 30, 255), a);
      const float m = size * 0.18f;
      dl->AddLine(ImVec2(p0.x + m, p0.y + m), ImVec2(p1.x - m, p1.y - m), red, S(9));
      dl->AddLine(ImVec2(p1.x - m, p0.y + m), ImVec2(p0.x + m, p1.y - m), red, S(9));
      continue;
    }
    // Tags: this Hokage's pick, and the other Hokage's.
    const char* tag = nullptr;
    ImU32 tag_color = kOrange;
    if (mine == kGridOrder[index]) {
      tag = picker_hokage_ == 0 ? "1ST" : "2ND";
    } else if (other == kGridOrder[index]) {
      tag = picker_hokage_ == 0 ? "2ND" : "1ST";
      tag_color = kBlue;
    }
    if (tag) OutlinedText(dl, ImVec2(p0.x + S(4), p1.y - S(30)), S(28), tag_color, tag, a);
  }

  // Right of the grid: the Hokage, the character under the cursor and the OFF button.
  const float px = x0 + kGridColumns * pitch + S(60.0f);
  float py = grid_y;
  if (const auto& portrait = pictures_[picker_hokage_]) {
    const float pic = S(200.0f);
    dl->AddImage(reinterpret_cast<ImTextureID>(portrait.get()), ImVec2(px, py), ImVec2(px + pic, py + pic),
                 ImVec2(0, 0), ImVec2(1, 1), WithAlpha(kWhite, alpha));
    py += pic + S(30.0f);
  }
  std::string name = "OFF", status = "FIGHT MODE KEEPS EVERY CHARACTER";
  if (grid_cell_ != kGridOff) {
    const int item = GridItemOf(grid_cell_);
    name = item ? page.items[item].label : "";
    status = mine == kGridOrder[grid_cell_]    ? "SELECTED"
             : other == kGridOrder[grid_cell_] ? std::string("TAKEN BY ") + kHokageRows[1 - picker_hokage_].first
                                               : "PRESS TO REPLACE";
  } else if (mine == "off") {
    status = "SELECTED";
  }
  OutlinedText(dl, ImVec2(px, py), S(48.0f), kWhite, name, alpha, 3.0f);
  OutlinedText(dl, ImVec2(px, py + S(60.0f)), S(30.0f), kBlue, status, alpha);

  // OFF, the cell right of the grid's last column.
  const float off_y = grid_y + (kGridRows - 1) * pitch;
  const bool off_selected = grid_cell_ == kGridOff;
  const ImVec2 off_size = TextSize(S(44.0f), "OFF");
  if (off_selected) {
    const float bob = std::sin(static_cast<float>(time_) * 7.0f) * S(4.0f);
    Arrow(dl, ImVec2(px + S(10.0f) + bob, off_y + cell * 0.5f), S(26.0f), kOrange, alpha);
  }
  OutlinedText(dl, ImVec2(px + S(40.0f), off_y + (cell - S(44.0f)) * 0.5f), S(44.0f),
               off_selected ? kOrange : kBlue, "OFF", alpha);
  if (io.MousePos.x >= px && io.MousePos.x < px + S(40.0f) + off_size.x + S(20.0f) && io.MousePos.y >= off_y &&
      io.MousePos.y < off_y + cell) {
    grid_hover_ = kGridOff;
  }

  // Button prompts, bottom right.
  const float prompt_y = h - S(80.0f);
  float right = w - S(110.0f);
  right = Prompt(dl, right, prompt_y, "BACK", 'B', "ESC", pad_prompts_, alpha);
  Prompt(dl, right, prompt_y, "SELECT", 'A', "ENTER", pad_prompts_, alpha);
}
