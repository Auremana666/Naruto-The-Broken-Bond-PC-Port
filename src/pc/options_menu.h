// In-game PC options menu (F1), drawn in the style of the game's own menus: the game's rounded
// font, blue items with an orange highlight and arrow, button prompts, and slide/fade animations.
// Navigation works with the keyboard, the mouse and a controller. Every change is written to
// naruto.toml next to the executable.

#pragma once

#include <chrono>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

struct ImFont;
struct ImDrawList;

// The game-style menu font, loaded by the app when ImGui fonts are configured (nullptr if missing).
extern ImFont* g_menu_font;

// Keyboard shortcuts: the PC Options menu, the FPS counter, and one per menu page.
struct Shortcut {
  const char* cvar;         // bind setting, saved in naruto.toml
  const char* label;
  const char* default_key;  // empty = no key
  const char* help;
  int page;                 // menu page it opens, -1 for none
};
inline const std::vector<Shortcut>& ShortcutList() {
  static const std::vector<Shortcut> list = {
      {"bind_options", "PC OPTIONS MENU", "F1", "Opens and closes this menu.", -1},
      {"bind_fps_counter", "FPS COUNTER", "F2", "Turns the frame rate counter on or off.", -1},
      {"bind_toggle_logs", "LOG FILES (WITH CTRL)", "F12", "Ctrl + this key turns log files in logs/ on or off.", -1},
      {"bind_debug_overlay", "ADVANCED FPS", "", "Detailed frame rate and performance overlay.", -1},
      {"bind_achievements", "ACHIEVEMENTS", "F7", "Shows the achievements you have unlocked.", -1},
      {"bind_speed_up", "SPEED UP (HOLD)", "NumpadPlus", "Hold to run the game faster, for cutscenes you cannot skip.", -1},
      {"bind_free_camera", "FREE CAMERA", "6",
       "Freezes the game and flies the camera: WASD, Space / Ctrl, Shift, mouse or arrows.", -1},
      {"bind_open_display", "OPEN DISPLAY", "", "Jumps straight to the Display page.", 1},
      {"bind_open_graphics", "OPEN GRAPHICS", "", "Jumps straight to the Graphics page.", 2},
      {"bind_open_controls", "OPEN CONTROLS", "", "Jumps straight to the Controls page.", 3},
      {"bind_open_key_bindings", "OPEN KEY BINDINGS", "", "Jumps straight to Key Bindings.", 4},
      {"bind_open_controller", "OPEN CONTROLLER LAYOUT", "", "Jumps straight to Controller Layout.", 7},
      {"bind_open_audio", "OPEN AUDIO", "", "Jumps straight to the Audio page.", 5},
      {"bind_open_game", "OPEN GAME", "", "Jumps straight to the Game page.", 6},
      {"bind_open_cheats", "OPEN CHEATS", "", "Jumps straight to the Cheats page.", 9},
  };
  return list;
}

// Writes the current values of the given settings into naruto.toml, keeping its comments.
bool SaveSettings(const std::filesystem::path& config_path, const std::set<std::string>& names);

class OptionsMenu : public rex::ui::ImGuiDialog {
 public:
  // The dialog deletes itself when closed; on_closed lets the owner drop its pointer.
  // on_exit quits the game (the Exit Game entry).
  OptionsMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path,
              std::function<void()> on_closed, std::function<void()> on_exit);

  // Writes the changed settings to naruto.toml.
  void Save();

  // Plays the closing animation, saves and closes the menu.
  void RequestClose();

  // Shows a page directly (shortcut keys). Back returns to its parent pages.
  void JumpTo(int page);

  // True while waiting for a key to bind; shortcut keys are ignored then.
  bool IsCapturing() const { return !capturing_.empty() || pad_capturing_ || jutsu_pad_capturing_; }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  struct Choice {
    std::string label;
    std::string value;
  };

  struct Item {
    enum class Kind { kChoice, kSlider, kSubmenu, kAction, kKeyBind };
    Kind kind = Kind::kChoice;
    std::string label;
    std::string cvar;                 // setting this item edits
    std::vector<std::string> mirror;  // more settings that always take the same value
    std::vector<Choice> choices;      // kChoice
    float min = 0.0f, max = 1.0f, step = 0.1f;  // kSlider
    int submenu = -1;                 // kSubmenu: page index
    std::function<void()> action;     // kAction
    std::string default_keys;         // kKeyBind
    std::string help;                 // one line shown at the bottom while selected
    bool single_key = false;          // kKeyBind: one key only (system binds such as F2)
    bool exit = false;                // the Exit Game entry, set apart at the bottom
    std::string mode_cvar;            // kKeyBind: press mode setting (left / right change it)
    std::function<std::string()> summary;  // kSubmenu: text shown in the value column
    int picture = -1;                 // index into pictures_, drawn left of the label
  };

  struct Page {
    std::string title;
    std::vector<Item> items;
    int selected = 0;
    int scroll = 0;
    bool controller = false;  // drawn as a controller picture instead of rows
    bool jutsu = false;       // the Auto Jutsu page: keyboard / controller 1 / controller 2 grid
    bool tall = false;        // tall rows with a picture each (Hidden Characters & Skins)
    bool grid = false;        // the Fight Mode select grid (the character a Hokage replaces)
  };

  struct Nav {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, back = false, clear = false, add = false;
    bool start = false;  // controller Start (keyboard R)
  };

  void BuildPages();
  Nav ReadInput(ImGuiIO& io);
  void HandleInput(const Nav& nav);
  void HandleMouse(ImGuiIO& io);
  void Activate(Item& item, int direction);
  void Capture(ImGuiIO& io, bool cancel);
  void OpenPage(int page);

  // The controller layout page.
  void ControllerFrame(ImGuiIO& io);
  void DrawController(ImDrawList* dl, ImGuiIO& io, float alpha);
  void SetPadTarget(int button, int target);

  // The Auto Jutsu page.
  void JutsuFrame(ImGuiIO& io);
  void DrawJutsu(ImDrawList* dl, ImGuiIO& io, float alpha);
  void ActivateJutsuCell();

  // The character grid page (a Hokage's replacement).
  void GridFrame(ImGuiIO& io);
  void DrawGrid(ImDrawList* dl, ImGuiIO& io, float alpha);

  int ChoiceIndex(const Item& item) const;
  std::string ValueText(const Item& item) const;
  void Draw(ImGuiIO& io);

  // Edits a setting and remembers to save.
  void Set(const std::string& name, const std::string& value);

  std::filesystem::path config_path_;
  std::function<void()> on_closed_;
  std::function<void()> on_exit_;
  bool exit_armed_ = false;  // Exit Game pressed once, waiting for confirmation
  std::vector<Page> pages_;
  std::vector<int> page_stack_;  // pages opened from the root, current page last
  int page_ = 0;

  std::string capturing_;     // keybind cvar being rebound, empty when idle
  std::vector<std::pair<std::string, std::string>> suspended_binds_;  // shortcuts off while binding
  std::string captured_shortcut_key_;  // key just given to a shortcut
  bool append_key_ = false;   // capturing a second key for the same action
  int capture_frames_ = 0;    // frames since capture started (ignores the key that started it)

  std::set<std::string> changed_;  // settings edited since the last save
  bool dirty_ = false;
  bool restart_needed_ = false;

  // Animation clocks, in seconds.
  double time_ = 0.0;
  double page_time_ = 0.0;
  double select_time_ = 0.0;
  double close_time_ = -1.0;  // >= 0 while the closing animation plays
  float flash_ = 0.0f;        // highlight pulse after a value changes

  // Controller state for edge detection and auto-repeat.
  uint32_t pad_held_ = 0;
  double pad_repeat_at_ = 0.0;

  // Controller layout page.
  int pad_selected_ = 0;            // layout::Button under the cursor
  int pad_hover_ = -1;              // button under the mouse
  bool pad_capturing_ = false;      // waiting for the button the selected one should send
  bool pad_capture_ready_ = false;  // every button released since the capture started
  int pad_capture_choice_ = 0;      // keyboard: target picked with left / right
  bool pad_reset_armed_ = false;    // Start pressed once, waiting for confirmation
  double pad_changed_time_ = -10.0;

  // Auto Jutsu page: rows are the on/off switch, the hold button, jutsu 1-4 and reset;
  // columns are the keyboard, controller 1 and controller 2.
  int jutsu_row_ = 0;
  int jutsu_col_ = 0;
  int jutsu_hover_row_ = -1;
  int jutsu_hover_col_ = -1;
  bool jutsu_pad_capturing_ = false;  // waiting for a button on controller jutsu_col_
  bool jutsu_pad_ready_ = false;      // that controller's buttons were all released first
  double jutsu_reset_time_ = -10.0;   // when Reset Auto Jutsu was last pressed

  // Hidden Characters & Skins: the Hokage portraits (resources 107 and 108) and the select grid
  // (109), all optional, and the Hokage whose replacement the character grid (page 14) is picking.
  std::array<std::unique_ptr<rex::ui::ImmediateTexture>, 3> pictures_;
  size_t picker_hokage_ = 0;
  int grid_cell_ = 1;      // the cursor on page 14: a grid cell, or kGridOff
  int grid_hover_ = -1;    // the cell under the mouse

  // Prompts show controller buttons after controller input, key names otherwise.
  bool pad_prompts_ = false;

  // Mouse: row under the cursor, and where it was last frame.
  int hover_row_ = -1;
  float last_mouse_x_ = -1.0f, last_mouse_y_ = -1.0f;
};
