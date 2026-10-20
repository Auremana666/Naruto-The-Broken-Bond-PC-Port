#pragma once

#include <imgui.h>

// Colors and building blocks shared by the launcher pages.
namespace launcher_ui {

inline constexpr ImVec4 kAccent(1.0f, 0.49f, 0.16f, 1.0f);
inline constexpr ImVec4 kGreen(0.48f, 0.84f, 0.64f, 1.0f);
inline constexpr ImVec4 kWarning(1.0f, 0.68f, 0.38f, 1.0f);
inline constexpr ImVec4 kPrimary(0.91f, 0.36f, 0.07f, 1.0f);
inline constexpr ImVec4 kPrimaryHovered(1.0f, 0.47f, 0.12f, 1.0f);
inline constexpr ImVec4 kPrimaryActive(0.76f, 0.28f, 0.04f, 1.0f);
inline constexpr ImVec4 kIdleTab(0.13f, 0.13f, 0.16f, 1.0f);

// The bold face at `size`; the default font when the bold face is missing.
void PushBoldFont(float size);

// One line in the bold face.
void Heading(const char* text, float size = 28.0f);

// Wrapped text in the disabled color.
void Muted(const char* text);

// A button in the accent color: the main action of a page.
bool PrimaryButton(const char* label, ImVec2 size);

// A bordered panel. Height 0 grows with the content.
void BeginCard(const char* id, float height);
void EndCard();

}  // namespace launcher_ui
