#include "cardputer_keyboard_map.h"
#include "key_event_mapper.h"
#include <cstdlib>
#include <iostream>

uint32_t hostMillis = 0;
namespace {
unsigned assertions = 0;
void check(bool condition, const char* message) {
  ++assertions;
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
void expect(unsigned row, unsigned col, bool fn, bool shift,
            input::KeyCode code, char character = 0) {
  Keyboard_Class::KeysState state;
  state.fn = fn; state.shift = shift;
  const auto result = input::mapCardputerKey(_key_value_map[row][col], state);
  check(result.code == code && result.character == character, "Wrong SDK key translation");
}
}

int main() {
  using input::KeyCode;
  bool printable[127]{};
  for (unsigned row = 0; row < 4; ++row) for (unsigned col = 0; col < 14; ++col) {
    for (bool shifted : {false, true}) {
      Keyboard_Class::KeysState state;
      state.shift = shifted;
      const auto event = input::mapCardputerKey(_key_value_map[row][col], state);
      if (event.code == KeyCode::Text) {
        const auto ch = static_cast<unsigned char>(event.character);
        check(ch >= 32 && ch <= 126, "Non-printable credential key");
        printable[ch] = true;
      }
    }
  }
  for (unsigned ch = 32; ch <= 126; ++ch) check(printable[ch], "Missing printable ASCII key");
  expect(0, 9, false, true, KeyCode::Text, '(');
  expect(0, 8, false, true, KeyCode::Text, '*');
  expect(0, 12, false, true, KeyCode::Text, '+');
  expect(0, 0, false, true, KeyCode::Text, '~');
  expect(2, 12, false, true, KeyCode::Text, '"');
  expect(1, 13, false, true, KeyCode::Text, '|');
  expect(2, 13, false, false, KeyCode::Select);
  expect(0, 13, false, false, KeyCode::Backspace);
  expect(1, 0, false, false, KeyCode::Down);
  expect(1, 0, false, true, KeyCode::Up);
  expect(2, 11, true, false, KeyCode::Up);
  expect(3, 11, true, false, KeyCode::Down);
  expect(3, 10, true, false, KeyCode::Left);
  expect(3, 12, true, false, KeyCode::Right);
  expect(0, 0, true, false, KeyCode::Back);
  expect(0, 13, true, false, KeyCode::Backspace);
  expect(2, 0, true, false, KeyCode::None);
  expect(2, 1, false, true, KeyCode::None);

  Keyboard_Class::KeysState state;
  state.ctrl = true;
  check(input::mapCardputerKey(_key_value_map[2][2], state).code == KeyCode::None,
        "Ctrl chord inserted credential text");
  state.ctrl = false; state.alt = true;
  check(input::mapCardputerKey(_key_value_map[2][2], state).code == KeyCode::None,
        "Alt chord inserted credential text");
  state.alt = false;
  check(input::mapCardputerKey(_key_value_map[2][2], state, true).character == 'A',
        "Caps-lock glyph not applied");

  // Fn and direction present in the first snapshot must emit one control only.
  input::KeyEventMapper mapper;
  input::KeyEvent keys[input::KeyEventMapper::KEY_COUNT]{};
  state.fn = true;
  keys[28] = input::mapCardputerKey(_key_value_map[2][0], state);
  keys[39] = input::mapCardputerKey(_key_value_map[2][11], state);
  mapper.update((uint64_t{1} << 28) | (uint64_t{1} << 39), keys, 10);
  input::KeyEvent out;
  check(mapper.pop(out) && out.code == KeyCode::Up, "Fn first-frame direction lost");
  check(!mapper.pop(out), "Fn first-frame generated extra printable key");
  std::cout << assertions << " SDK keyboard assertions passed; all 95 printable ASCII characters reachable\n";
}
