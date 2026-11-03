#pragma once
// Logical key events, shared by the TCA8418 driver (hardware) and the simulator.
#include <stdint.h>

enum class KeyCode : uint8_t {
  None = 0,
  Char,       // printable character in KeyEvent::ch (UTF-8 is not produced by the keyboard: ASCII only)
  Enter,
  Backspace,
  Tab,
  Esc,        // Fn + `
  Up,         // Fn + ;
  Down,       // Fn + .
  Left,       // Fn + ,
  Right,      // Fn + /
};

enum KeyMod : uint8_t {
  MOD_NONE  = 0,
  MOD_SHIFT = 1 << 0,
  MOD_FN    = 1 << 1,
  MOD_CTRL  = 1 << 2,
  MOD_ALT   = 1 << 3,
  MOD_OPT   = 1 << 4,
};

struct KeyEvent {
  KeyCode code = KeyCode::None;
  char ch = 0;        // valid when code == Char (already shifted, e.g. 'A', '!')
  uint8_t mods = 0;   // KeyMod bitmask held at the time of the press

  bool is(char c) const { return code == KeyCode::Char && (ch == c || ch == (c ^ 0x20)); }
};

// On screens that do not take text input, the bare arrow-printed keys ; . , /
// and ` act as arrows / Esc, so the user doesn't need to hold Fn.
// Text-entry screens get the raw characters and must use Fn for navigation.
inline KeyEvent toNavigation(KeyEvent e) {
  if (e.code != KeyCode::Char) return e;
  KeyEvent n = e;
  switch (e.ch) {
    case ';': n.code = KeyCode::Up; break;
    case '.': n.code = KeyCode::Down; break;
    case ',': n.code = KeyCode::Left; break;
    case '/': n.code = KeyCode::Right; break;
    case '`': n.code = KeyCode::Esc; break;
    default: return e;
  }
  n.ch = 0;
  return n;
}
