#include "KeyMatrix.h"

namespace {

// Special entries of the layout table
enum : uint8_t {
  K_NONE = 0,
  K_ESC = 0x1B,
  K_BKSP = 0x08,
  K_TAB = 0x09,
  K_ENTER = 0x0A,
  K_FN = 0x80,
  K_SHIFT,
  K_CTRL,
  K_OPT,
  K_ALT,
  K_UP,
  K_DOWN,
  K_LEFT,
  K_RIGHT,
  K_DEL,
};

struct KeyDef {
  uint8_t normal, shifted, fn;
};

// Same layout as M5Cardputer's _key_value_map (4 rows x 14 columns).
const KeyDef LAYOUT[KeyMatrix::ROWS][KeyMatrix::COLS] = {
  {{'`', '~', K_ESC}, {'1', '!', 0}, {'2', '@', 0}, {'3', '#', 0}, {'4', '$', 0}, {'5', '%', 0},
   {'6', '^', 0}, {'7', '&', 0}, {'8', '*', 0}, {'9', '(', 0}, {'0', ')', 0}, {'-', '_', 0},
   {'=', '+', 0}, {K_BKSP, K_BKSP, K_DEL}},
  {{K_TAB, K_TAB, 0}, {'q', 'Q', 0}, {'w', 'W', 0}, {'e', 'E', 0}, {'r', 'R', 0}, {'t', 'T', 0},
   {'y', 'Y', 0}, {'u', 'U', 0}, {'i', 'I', 0}, {'o', 'O', 0}, {'p', 'P', 0}, {'[', '{', 0},
   {']', '}', 0}, {'\\', '|', 0}},
  {{K_FN, K_FN, K_FN}, {K_SHIFT, K_SHIFT, 0}, {'a', 'A', 0}, {'s', 'S', 0}, {'d', 'D', 0},
   {'f', 'F', 0}, {'g', 'G', 0}, {'h', 'H', 0}, {'j', 'J', 0}, {'k', 'K', 0}, {'l', 'L', 0},
   {';', ':', K_UP}, {'\'', '"', 0}, {K_ENTER, K_ENTER, 0}},
  {{K_CTRL, K_CTRL, 0}, {K_OPT, K_OPT, 0}, {K_ALT, K_ALT, 0}, {'z', 'Z', 0}, {'x', 'X', 0},
   {'c', 'C', 0}, {'v', 'V', 0}, {'b', 'B', 0}, {'n', 'N', 0}, {'m', 'M', 0}, {',', '<', K_LEFT},
   {'.', '>', K_DOWN}, {'/', '?', K_RIGHT}, {' ', ' ', 0}},
};

uint8_t modBit(uint8_t k) {
  switch (k) {
    case K_FN: return MOD_FN;
    case K_SHIFT: return MOD_SHIFT;
    case K_CTRL: return MOD_CTRL;
    case K_OPT: return MOD_OPT;
    case K_ALT: return MOD_ALT;
    default: return 0;
  }
}

}  // namespace

bool KeyMatrix::toGrid(uint8_t keyNumber, int& row, int& col) {
  if (keyNumber < 1 || keyNumber > 80) return false;
  // TCA8418 numbering: key = tcaRow * 10 + tcaCol + 1 (7 rows x 8 cols used)
  int n = keyNumber - 1;
  int tcaRow = n / 10, tcaCol = n % 10;
  if (tcaRow >= 7 || tcaCol >= 8) return false;
  col = tcaRow * 2 + (tcaCol > 3 ? 1 : 0);
  row = tcaCol % 4;
  return true;
}

uint8_t KeyMatrix::fromGrid(int row, int col) {
  for (uint8_t k = 1; k <= 80; k++) {
    int r, c;
    if (toGrid(k, r, c) && r == row && c == col) return k;
  }
  return 0;
}

bool KeyMatrix::feed(uint8_t tcaEvent, KeyEvent& out) {
  bool pressed = tcaEvent & 0x80;
  int row, col;
  if (!toGrid(tcaEvent & 0x7F, row, col)) return false;
  const KeyDef& d = LAYOUT[row][col];

  if (uint8_t m = modBit(d.normal)) {
    if (pressed) _mods |= m; else _mods &= ~m;
    return false;
  }
  if (!pressed) return false;

  out = KeyEvent();
  out.mods = _mods;
  uint8_t k;
  if ((_mods & MOD_FN) && d.fn) k = d.fn;
  else if (_mods & MOD_SHIFT) k = d.shifted;
  else k = d.normal;

  switch (k) {
    case K_NONE: return false;
    case K_ESC: out.code = KeyCode::Esc; break;
    case K_BKSP: case K_DEL: out.code = KeyCode::Backspace; break;
    case K_TAB: out.code = KeyCode::Tab; break;
    case K_ENTER: out.code = KeyCode::Enter; break;
    case K_UP: out.code = KeyCode::Up; break;
    case K_DOWN: out.code = KeyCode::Down; break;
    case K_LEFT: out.code = KeyCode::Left; break;
    case K_RIGHT: out.code = KeyCode::Right; break;
    default:
      out.code = KeyCode::Char;
      out.ch = (char)k;
      break;
  }
  return true;
}
