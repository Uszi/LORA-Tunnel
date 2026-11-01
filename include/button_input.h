#pragma once

#include <cstdint>

namespace tunnel {

enum class ButtonEvent : std::uint8_t { None, ShortPress, LongPress };

// Poll from one task with a monotonic millisecond counter. The caller supplies
// logical pressed state, keeping pin polarity outside this portable class.
// A short press is reported on debounced release; a long press fires once while
// held and suppresses that release. There is no repeat or dynamic allocation.
class ButtonInput {
public:
  static constexpr std::uint32_t DebounceMs = 30;
  static constexpr std::uint32_t LongPressMs = 700;

  explicit ButtonInput(bool longPressEnabled = true)
      : longPressEnabled_(longPressEnabled) {}

  void reset(std::uint32_t now, bool pressed) {
    initialized_ = true;
    rawPressed_ = stablePressed_ = pressed;
    rawSince_ = pressedSince_ = now;
    // A key held during initialization must be released before it can act.
    armed_ = !pressed;
    activePress_ = longFired_ = false;
  }

  ButtonEvent update(std::uint32_t now, bool pressed) {
    if (!initialized_) {
      reset(now, pressed);
      return ButtonEvent::None;
    }
    if (pressed != rawPressed_) {
      rawPressed_ = pressed;
      rawSince_ = now;
    }
    if (rawPressed_ != stablePressed_ && elapsed(now, rawSince_) >= DebounceMs) {
      stablePressed_ = rawPressed_;
      if (stablePressed_) {
        pressedSince_ = now;
        activePress_ = armed_;
        longFired_ = false;
      } else {
        const bool wasActive = activePress_;
        activePress_ = false;
        armed_ = true;
        if (wasActive && !longFired_) {
          // Use the first observed release edge, not the debounce delay. This
          // also handles a slow polling cycle that missed the hold threshold.
          if (longPressEnabled_ && elapsed(rawSince_, pressedSince_) >= LongPressMs)
            return ButtonEvent::LongPress;
          return ButtonEvent::ShortPress;
        }
      }
    }
    // Do not turn a short press into a long one while its release is debouncing.
    if (activePress_ && stablePressed_ && rawPressed_ && longPressEnabled_ &&
        !longFired_ && elapsed(now, pressedSince_) >= LongPressMs) {
      longFired_ = true;
      return ButtonEvent::LongPress;
    }
    return ButtonEvent::None;
  }

private:
  static std::uint32_t elapsed(std::uint32_t now, std::uint32_t then) {
    return now - then; // Unsigned subtraction remains correct at millis wrap.
  }

  const bool longPressEnabled_;
  bool initialized_ = false;
  bool rawPressed_ = false;
  bool stablePressed_ = false;
  bool armed_ = false;
  bool activePress_ = false;
  bool longFired_ = false;
  std::uint32_t rawSince_ = 0;
  std::uint32_t pressedSince_ = 0;
};

} // namespace tunnel
