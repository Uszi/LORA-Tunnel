#pragma once

#include <cstdint>

namespace input {
enum class KeyCode : uint8_t {
  None, Up, Down, Left, Right, Select, Back, Backspace, Text
};
struct KeyEvent {
  KeyCode code = KeyCode::None;
  char character = 0;
};
}
