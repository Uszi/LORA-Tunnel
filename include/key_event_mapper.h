#pragma once

#include "keyboard_input.h"
#include <cstddef>

namespace input {
// Hardware-independent transition/repeat handling. Physical positions, rather
// than key count or printable characters, prevent missed equal-count changes
// and modifier-only retyping. The adapter supplies at most 56 physical keys.
class KeyEventMapper {
public:
  static constexpr size_t KEY_COUNT = 56;
  static constexpr size_t QUEUE_SIZE = 64;
  static constexpr uint32_t REPEAT_DELAY_MS = 400;
  static constexpr uint32_t REPEAT_INTERVAL_MS = 80;

  void reset() { *this = KeyEventMapper{}; }

  void update(uint64_t down, const KeyEvent (&keys)[KEY_COUNT], uint32_t now) {
    for (size_t i = 0; i < KEY_COUNT; ++i) {
      const uint64_t bit = uint64_t{1} << i;
      const bool held = (down & bit) != 0;
      if (!held) {
        codes_[i] = KeyCode::None;
        continue;
      }
      const auto event = keys[i];
      const bool pressed = !(down_ & bit);
      const bool controlChanged = event.code != codes_[i] &&
          event.code != KeyCode::Text && event.code != KeyCode::None;
      if (pressed || controlChanged) {
        push(event);
        repeatAt_[i] = now + REPEAT_DELAY_MS;
      } else if (repeatable(event.code) &&
                 static_cast<int32_t>(now - repeatAt_[i]) >= 0) {
        push(event);
        // Skip missed intervals: a delayed UI tick must not flood navigation.
        repeatAt_[i] = now + REPEAT_INTERVAL_MS;
      }
      codes_[i] = event.code;
    }
    down_ = down;
  }

  bool pop(KeyEvent& event) {
    if (!count_) return false;
    event = queue_[head_];
    head_ = (head_ + 1) % QUEUE_SIZE;
    --count_;
    return true;
  }

private:
  static bool repeatable(KeyCode code) {
    return code == KeyCode::Up || code == KeyCode::Down ||
        code == KeyCode::Left || code == KeyCode::Right ||
        code == KeyCode::Backspace;
  }
  void push(KeyEvent event) {
    if (event.code == KeyCode::None || count_ == QUEUE_SIZE) return;
    queue_[(head_ + count_) % QUEUE_SIZE] = event;
    ++count_;
  }
  uint64_t down_ = 0;
  KeyCode codes_[KEY_COUNT]{};
  uint32_t repeatAt_[KEY_COUNT]{};
  KeyEvent queue_[QUEUE_SIZE]{};
  size_t head_ = 0;
  size_t count_ = 0;
};
}
