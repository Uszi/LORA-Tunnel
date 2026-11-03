#include <unity.h>
#include "key_event_mapper.h"

using input::KeyCode;
using input::KeyEvent;
using input::KeyEventMapper;

void setUp() {}
void tearDown() {}

void physical_transitions_survive_equal_key_counts() {
  KeyEventMapper mapper;
  KeyEvent keys[KeyEventMapper::KEY_COUNT]{};
  keys[0] = {KeyCode::Text, 'a'};
  keys[1] = {KeyCode::Text, 'b'};
  mapper.update(1, keys, 0);
  KeyEvent out;
  TEST_ASSERT_TRUE(mapper.pop(out));
  TEST_ASSERT_EQUAL_CHAR('a', out.character);
  mapper.update(2, keys, 10);
  TEST_ASSERT_TRUE(mapper.pop(out));
  TEST_ASSERT_EQUAL_CHAR('b', out.character);
  TEST_ASSERT_FALSE(mapper.pop(out));
}

void held_text_and_modifier_changes_do_not_retype() {
  KeyEventMapper mapper;
  KeyEvent keys[KeyEventMapper::KEY_COUNT]{};
  keys[0] = {KeyCode::Text, 'a'};
  mapper.update(1, keys, 0);
  KeyEvent out;
  TEST_ASSERT_TRUE(mapper.pop(out));
  keys[0].character = 'A';
  mapper.update(3, keys, 50); // a held plus Shift, which has no event.
  mapper.update(3, keys, 1000);
  TEST_ASSERT_FALSE(mapper.pop(out));
  mapper.update(0, keys, 1010);
  mapper.update(1, keys, 1020);
  TEST_ASSERT_TRUE(mapper.pop(out));
  TEST_ASSERT_EQUAL_CHAR('A', out.character);
}

void controls_repeat_bounded_without_catchup_burst() {
  KeyEventMapper mapper;
  KeyEvent keys[KeyEventMapper::KEY_COUNT]{};
  keys[0] = {KeyCode::Down, 0};
  mapper.update(1, keys, 10);
  KeyEvent out;
  TEST_ASSERT_TRUE(mapper.pop(out));
  mapper.update(1, keys, 409);
  TEST_ASSERT_FALSE(mapper.pop(out));
  mapper.update(1, keys, 410);
  TEST_ASSERT_TRUE(mapper.pop(out));
  mapper.update(1, keys, 8000);
  TEST_ASSERT_TRUE(mapper.pop(out));
  TEST_ASSERT_FALSE(mapper.pop(out));
  mapper.update(0, keys, 8001);
  mapper.update(0, keys, 9000);
  TEST_ASSERT_FALSE(mapper.pop(out));
}

void select_never_repeats_and_fn_transition_is_navigation() {
  KeyEventMapper mapper;
  KeyEvent keys[KeyEventMapper::KEY_COUNT]{};
  keys[0] = {KeyCode::Select, 0};
  mapper.update(1, keys, 0);
  KeyEvent out;
  TEST_ASSERT_TRUE(mapper.pop(out));
  mapper.update(1, keys, 2000);
  TEST_ASSERT_FALSE(mapper.pop(out));
  keys[0] = {KeyCode::Up, 0};
  mapper.update(1, keys, 2010);
  TEST_ASSERT_TRUE(mapper.pop(out));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(KeyCode::Up),
                         static_cast<uint8_t>(out.code));
}

void timer_wrap_and_backspace_repeat() {
  KeyEventMapper mapper;
  KeyEvent keys[KeyEventMapper::KEY_COUNT]{};
  keys[0] = {KeyCode::Backspace, 0};
  const uint32_t start = 0xffffff00U;
  mapper.update(1, keys, start);
  KeyEvent out;
  TEST_ASSERT_TRUE(mapper.pop(out));
  mapper.update(1, keys, start + 399U);
  TEST_ASSERT_FALSE(mapper.pop(out));
  mapper.update(1, keys, start + 400U);
  TEST_ASSERT_TRUE(mapper.pop(out));
  mapper.reset();
  TEST_ASSERT_FALSE(mapper.pop(out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(physical_transitions_survive_equal_key_counts);
  RUN_TEST(held_text_and_modifier_changes_do_not_retype);
  RUN_TEST(controls_repeat_bounded_without_catchup_burst);
  RUN_TEST(select_never_repeats_and_fn_transition_is_navigation);
  RUN_TEST(timer_wrap_and_backspace_repeat);
  return UNITY_END();
}
