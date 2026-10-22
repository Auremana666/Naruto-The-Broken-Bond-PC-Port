// The PC title screen shown when the game starts (PC Options > Game > PC Title Screen): white,
// the game's logo with "PC" above it, how to open PC Options, a flashing PRESS ENTER, the
// credits, and Naruto on the right. Enter (or Start / A on a controller, or a click) continues;
// after 30 seconds it continues by itself. The game boots underneath and gets no input meanwhile.
//
// Naruto and the game logo are built into the executable (resources 101 and 104, from
// assets/title/). A title/logo.png next to the executable replaces the logo.
//
// There is also a hidden extra: a secret word typed here changes the credits, the character
// (resource 102) and plays a sound (resource 103, an MP3). Those resources come from
// assets/secret/, which is not part of the public source; without them the extra is off.

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

class TitleScreen : public rex::ui::ImGuiDialog {
 public:
  // on_closed runs once the screen has faded out.
  TitleScreen(rex::ui::ImGuiDrawer* drawer, std::filesystem::path folder,
              std::function<void()> on_closed);
  ~TitleScreen() override;

  // Fades out and closes (also what Enter does).
  void Dismiss();

  // True from the moment the screen is created until it has closed (any thread).
  static bool IsOpen();

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  struct Picture {
    std::unique_ptr<rex::ui::ImmediateTexture> texture;
    int width = 0;
    int height = 0;
  };
  Picture Load(const std::filesystem::path& path);
  Picture LoadResource(int id);
  Picture Upload(unsigned char* pixels, int width, int height);

  std::function<void()> on_closed_;
  Picture naruto_;
  Picture logo_;
  double time_ = 0.0;
  double close_time_ = -1.0;  // >= 0 while fading out
  uint32_t pad_held_ = 0;
  Picture extra_;
  std::string typed_;          // the last letters typed, for the easter egg
  double secret_time_ = -1.0;  // >= 0 once the secret word was typed
  std::string secret_name_;    // the word, shown in the credits
  std::wstring sound_file_;    // the sound, written to a temporary file while it plays

  void TriggerSecret();
  void StopSound();
};
