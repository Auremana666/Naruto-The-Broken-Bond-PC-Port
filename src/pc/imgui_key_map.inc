
static rex::ui::VirtualKey ImGuiKeyToVirtualKey(ImGuiKey key) {
  using VK = rex::ui::VirtualKey;
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
    return static_cast<VK>(static_cast<uint16_t>(VK::kA) + (key - ImGuiKey_A));
  }
  if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
    return static_cast<VK>(static_cast<uint16_t>(VK::k0) + (key - ImGuiKey_0));
  }
  if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24) {
    return static_cast<VK>(static_cast<uint16_t>(VK::kF1) + (key - ImGuiKey_F1));
  }
  if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) {
    return static_cast<VK>(static_cast<uint16_t>(VK::kNumpad0) + (key - ImGuiKey_Keypad0));
  }
  switch (key) {
    case ImGuiKey_Space:
      return VK::kSpace;
    case ImGuiKey_Enter:
      return VK::kReturn;
    case ImGuiKey_Escape:
      return VK::kEscape;
    case ImGuiKey_Tab:
      return VK::kTab;
    case ImGuiKey_Backspace:
      return VK::kBack;
    case ImGuiKey_Delete:
      return VK::kDelete;
    case ImGuiKey_Insert:
      return VK::kInsert;
    case ImGuiKey_Home:
      return VK::kHome;
    case ImGuiKey_End:
      return VK::kEnd;
    case ImGuiKey_PageUp:
      return VK::kPrior;
    case ImGuiKey_PageDown:
      return VK::kNext;
    case ImGuiKey_LeftArrow:
      return VK::kLeft;
    case ImGuiKey_RightArrow:
      return VK::kRight;
    case ImGuiKey_UpArrow:
      return VK::kUp;
    case ImGuiKey_DownArrow:
      return VK::kDown;
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift:
      return VK::kShift;
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl:
      return VK::kControl;
    case ImGuiKey_LeftAlt:
    case ImGuiKey_RightAlt:
      return VK::kMenu;
    case ImGuiKey_CapsLock:
      return VK::kCapital;
    case ImGuiKey_NumLock:
      return VK::kNumLock;
    case ImGuiKey_ScrollLock:
      return VK::kScroll;
    case ImGuiKey_PrintScreen:
      return VK::kSnapshot;
    case ImGuiKey_Pause:
      return VK::kPause;
    case ImGuiKey_GraveAccent:
      return VK::kOem3;
    case ImGuiKey_Minus:
      return VK::kOemMinus;
    case ImGuiKey_Equal:
      return VK::kOemPlus;
    case ImGuiKey_LeftBracket:
      return VK::kOem4;
    case ImGuiKey_RightBracket:
      return VK::kOem6;
    case ImGuiKey_Backslash:
      return VK::kOem5;
    case ImGuiKey_Semicolon:
      return VK::kOem1;
    case ImGuiKey_Apostrophe:
      return VK::kOem7;
    case ImGuiKey_Comma:
      return VK::kOemComma;
    case ImGuiKey_Period:
      return VK::kOemPeriod;
    case ImGuiKey_Slash:
      return VK::kOem2;
    case ImGuiKey_KeypadDecimal:
      return VK::kDecimal;
    case ImGuiKey_KeypadDivide:
      return VK::kDivide;
    case ImGuiKey_KeypadMultiply:
      return VK::kMultiply;
    case ImGuiKey_KeypadSubtract:
      return VK::kSubtract;
    case ImGuiKey_KeypadAdd:
      return VK::kAdd;
    case ImGuiKey_KeypadEnter:
      return VK::kReturn;
    default:
      return VK::kNone;
  }
}

