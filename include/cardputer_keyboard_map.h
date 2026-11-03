#pragma once

#include "keyboard_input.h"
#include <utility/Keyboard/Keyboard.h>

namespace input {
// Consume the pinned SDK's physical map and its modifier-first snapshot. In
// that map special key IDs overlap ASCII, so classify the unshifted physical
// code before interpreting a shifted printable glyph such as (, * or +.
inline KeyEvent mapCardputerKey(const KeyValue_t& value,
                               const Keyboard_Class::KeysState& state,
                               bool capsLocked = false) {
  if (state.fn) {
    switch (static_cast<uint8_t>(value.value_third)) {
      case KEY_UP: return {KeyCode::Up, 0};
      case KEY_DOWN: return {KeyCode::Down, 0};
      case KEY_LEFT: return {KeyCode::Left, 0};
      case KEY_RIGHT: return {KeyCode::Right, 0};
      case KEY_ESCAPE: return {KeyCode::Back, 0};
      case KEY_DELETE: return {KeyCode::Backspace, 0};
      default: return {};
    }
  }
  switch (static_cast<uint8_t>(value.value_first)) {
    case KEY_ENTER: return {KeyCode::Select, 0};
    case KEY_BACKSPACE: return {KeyCode::Backspace, 0};
    case KEY_TAB: return {state.shift ? KeyCode::Up : KeyCode::Down, 0};
    case KEY_FN: case KEY_OPT: case KEY_LEFT_CTRL: case KEY_LEFT_ALT:
    case KEY_LEFT_SHIFT: return {};
    default: break;
  }
  if (state.ctrl || state.alt) return {};
  const char ch = state.shift || capsLocked ? value.value_second : value.value_first;
  return ch >= 32 && ch <= 126 ? KeyEvent{KeyCode::Text, ch} : KeyEvent{};
}
}
