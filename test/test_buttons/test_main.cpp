#include <cstdint>
#include <unity.h>

#include "button_input.h"

using tunnel::ButtonEvent;
using tunnel::ButtonInput;

void setUp() {}
void tearDown() {}

static void expect(ButtonEvent expected, ButtonInput& button, std::uint32_t now, bool pressed) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(expected),
                         static_cast<std::uint8_t>(button.update(now, pressed)));
}

void test_short_press_only_emits_after_stable_release() {
  ButtonInput button;
  button.reset(0, false);
  expect(ButtonEvent::None, button, 10, true);
  expect(ButtonEvent::None, button, 39, true);
  expect(ButtonEvent::None, button, 40, true);
  expect(ButtonEvent::None, button, 100, false);
  expect(ButtonEvent::None, button, 129, false);
  expect(ButtonEvent::ShortPress, button, 130, false);
  expect(ButtonEvent::None, button, 131, false);
}

void test_bounce_does_not_create_or_repeat_press() {
  ButtonInput button;
  button.reset(0, false);
  expect(ButtonEvent::None, button, 10, true);
  expect(ButtonEvent::None, button, 20, false);
  expect(ButtonEvent::None, button, 25, true);
  expect(ButtonEvent::None, button, 54, true);
  expect(ButtonEvent::None, button, 55, true);
  expect(ButtonEvent::None, button, 100, false);
  expect(ButtonEvent::None, button, 110, true);
  expect(ButtonEvent::None, button, 120, false);
  expect(ButtonEvent::None, button, 149, false);
  expect(ButtonEvent::ShortPress, button, 150, false);
  expect(ButtonEvent::None, button, 200, false);
}

void test_glitch_shorter_than_debounce_is_ignored() {
  ButtonInput button;
  button.reset(0, false);
  expect(ButtonEvent::None, button, 10, true);
  expect(ButtonEvent::None, button, 39, false);
  expect(ButtonEvent::None, button, 100, false);
}

void test_long_press_emits_once_and_release_is_suppressed() {
  ButtonInput button;
  button.reset(0, false);
  expect(ButtonEvent::None, button, 10, true);
  expect(ButtonEvent::None, button, 40, true);
  expect(ButtonEvent::None, button, 739, true);
  expect(ButtonEvent::LongPress, button, 740, true);
  expect(ButtonEvent::None, button, 3000, true);
  expect(ButtonEvent::None, button, 3010, false);
  expect(ButtonEvent::None, button, 3040, false);
  expect(ButtonEvent::None, button, 4000, false);
}

void test_release_debounce_does_not_extend_short_press_into_long() {
  ButtonInput button;
  button.reset(0, false);
  expect(ButtonEvent::None, button, 10, true);
  expect(ButtonEvent::None, button, 40, true);
  expect(ButtonEvent::None, button, 739, false);
  expect(ButtonEvent::None, button, 740, false);
  expect(ButtonEvent::ShortPress, button, 769, false);
}

void test_slow_poll_still_classifies_long_hold_on_release() {
  ButtonInput button;
  button.reset(0, false);
  expect(ButtonEvent::None, button, 10, true);
  expect(ButtonEvent::None, button, 40, true);
  expect(ButtonEvent::None, button, 900, false);
  expect(ButtonEvent::LongPress, button, 930, false);
  expect(ButtonEvent::None, button, 1000, false);
}

void test_boot_held_input_is_ignored_until_release_and_new_press() {
  ButtonInput button;
  expect(ButtonEvent::None, button, 0, true);
  expect(ButtonEvent::None, button, 2000, true);
  expect(ButtonEvent::None, button, 2010, false);
  expect(ButtonEvent::None, button, 2040, false);
  expect(ButtonEvent::None, button, 2100, true);
  expect(ButtonEvent::None, button, 2130, true);
  expect(ButtonEvent::None, button, 2200, false);
  expect(ButtonEvent::ShortPress, button, 2230, false);
}

void test_io_button_has_no_hold_action_or_auto_repeat() {
  ButtonInput button(false);
  button.reset(0, false);
  expect(ButtonEvent::None, button, 10, true);
  expect(ButtonEvent::None, button, 40, true);
  expect(ButtonEvent::None, button, 740, true);
  expect(ButtonEvent::None, button, 10000, true);
  expect(ButtonEvent::None, button, 10010, false);
  expect(ButtonEvent::ShortPress, button, 10040, false);
  expect(ButtonEvent::None, button, 10070, false);
}

void test_short_press_debounce_across_millis_wrap() {
  ButtonInput button;
  button.reset(UINT32_MAX - 20, false);
  expect(ButtonEvent::None, button, UINT32_MAX - 10, true);
  expect(ButtonEvent::None, button, 18, true);
  expect(ButtonEvent::None, button, 19, true);
  expect(ButtonEvent::None, button, 30, false);
  expect(ButtonEvent::ShortPress, button, 60, false);
}

void test_long_hold_across_millis_wrap() {
  ButtonInput button;
  button.reset(UINT32_MAX - 100, false);
  expect(ButtonEvent::None, button, UINT32_MAX - 80, true);
  expect(ButtonEvent::None, button, UINT32_MAX - 50, true);
  expect(ButtonEvent::None, button, 648, true);
  expect(ButtonEvent::LongPress, button, 649, true);
  expect(ButtonEvent::None, button, 700, false);
  expect(ButtonEvent::None, button, 730, false);
}

void test_new_press_after_long_release_is_not_suppressed() {
  ButtonInput button;
  button.reset(0, false);
  expect(ButtonEvent::None, button, 10, true);
  expect(ButtonEvent::None, button, 40, true);
  expect(ButtonEvent::LongPress, button, 740, true);
  expect(ButtonEvent::None, button, 750, false);
  expect(ButtonEvent::None, button, 780, false);
  expect(ButtonEvent::None, button, 800, true);
  expect(ButtonEvent::None, button, 830, true);
  expect(ButtonEvent::None, button, 900, false);
  expect(ButtonEvent::ShortPress, button, 930, false);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_short_press_only_emits_after_stable_release);
  RUN_TEST(test_bounce_does_not_create_or_repeat_press);
  RUN_TEST(test_glitch_shorter_than_debounce_is_ignored);
  RUN_TEST(test_long_press_emits_once_and_release_is_suppressed);
  RUN_TEST(test_release_debounce_does_not_extend_short_press_into_long);
  RUN_TEST(test_slow_poll_still_classifies_long_hold_on_release);
  RUN_TEST(test_boot_held_input_is_ignored_until_release_and_new_press);
  RUN_TEST(test_io_button_has_no_hold_action_or_auto_repeat);
  RUN_TEST(test_short_press_debounce_across_millis_wrap);
  RUN_TEST(test_long_hold_across_millis_wrap);
  RUN_TEST(test_new_press_after_long_release_is_not_suppressed);
  return UNITY_END();
}
