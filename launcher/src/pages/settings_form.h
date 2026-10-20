#pragma once

#include <imgui.h>

#include <span>

#include "localization.h"

// The rows of the settings page and the panel that describes them.
namespace launcher_ui {

// The renderer that uses an option.
enum class Renderer { kBoth, kXenos, kNative };

// One row of the settings: its label, the text the description panel shows
// for it, and the renderer that uses it. Rows are told apart by the address
// of their Option, so options are defined once, with static storage.
struct Option {
  TextId label;
  TextId description;
  Renderer renderer = Renderer::kBoth;
  // Shown instead of `description` while the native renderer is chosen.
  TextId native_description = TextId::Count;
  // Keep a reference control visible while its game contract is unresolved.
  bool available = true;
};

// Draws the rows of one group, one under the other: the label on the left and
// the control on the right (a checkbox carries its own label). The options of
// the renderer that is not chosen are greyed out and tagged, and the native
// renderer's options are left out of builds without it.
//
// The row under the mouse (or, while the keyboard navigates, the focused row)
// is highlighted and remembered across frames; Described() is the option for
// the description panel.
class OptionList {
 public:
  explicit OptionList(bool native);
  ~OptionList();
  OptionList(const OptionList&) = delete;
  OptionList& operator=(const OptionList&) = delete;

  // Each returns whether the user changed the value in this frame.
  bool Combo(const Option& option, int& index, std::span<const char* const> items);
  bool Checkbox(const Option& option, bool& value);
  // A thin track filled up to a handle, with the value beside it, written
  // with `format` ("%d%%" for a percentage).
  bool Slider(const Option& option, int& value, int min, int max, const char* format = "%d");
  // The same track over a set of values in ascending order, the first at the
  // start and the last at the end: the handle sits on the one nearest to
  // `value` and snaps from one to the next.
  bool Slider(const Option& option, int& value, std::span<const int> stops);
  // A value that is shown, not edited.
  void Text(const Option& option, const char* text);
  // A button across the control column; returns whether it was clicked.
  bool Button(const Option& option, const char* text);

  // The row pointed at in this frame, else the remembered one when this group
  // drew it, else the group's first.
  const Option* Described() const;

 private:
  bool BeginRow(const Option& option);
  void EndRow(const Option& option);
  // The track of both sliders: `value` from min to max, or over `stops` when
  // there are any.
  bool Track(const Option& option, int& value, int min, int max, std::span<const int> stops,
             const char* format);
  void LabelColumn(const Option& option);
  // The "only in one renderer" tag of an unavailable option, beside the last item.
  void DrawTag(const Option& option, float x, bool right_aligned) const;

  bool native_;
  float row_width_;
  float label_width_;
  float widest_label_ = 0;
  const Option* first_ = nullptr;
  const Option* pointed_ = nullptr;
  bool remembered_drawn_ = false;
  ImVec2 row_start_;
};

// The description panel: the option's name, its description and, when the
// chosen renderer does not use it, why.
void RenderDescription(const Option& option, bool native);

}  // namespace launcher_ui
