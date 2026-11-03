#include "HwRig.h"
#include "../core/KeyMatrix.h"
#include <string.h>

namespace {
// Official Cardputer layout (normal / shifted), 0 = not a character key
const char* const NORMAL[4] = {"`1234567890-=\x01", "\x01qwertyuiop[]\\", "\x01\x01" "asdfghjkl;'\x01",
                               "\x01\x01\x01zxcvbnm,./ "};
const char* const SHIFTED[4] = {"~!@#$%^&*()_+\x01", "\x01QWERTYUIOP{}|", "\x01\x01" "ASDFGHJKL:\"\x01",
                                "\x01\x01\x01ZXCVBNM<>? "};
struct Pos { int r, c; };
constexpr Pos FN{2, 0}, SHIFT{2, 1}, CTRL{3, 0}, OPT{3, 1}, ALT{3, 2};

bool findChar(char ch, Pos& p, bool& shift) {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 14; c++) {
      if (NORMAL[r][c] == ch) { p = {r, c}; shift = false; return true; }
      if (SHIFTED[r][c] == ch) { p = {r, c}; shift = true; return true; }
    }
  return false;
}
}  // namespace

HwRig::HwRig(SimSystem& sys)
    : kb(bus, &kbChip), gps(gpsUart, &_clock), _sys(sys), _clock(sys), _gpsSvc(sys, gps) {
  bus.attach(Tca8418::ADDR, &kbChip);
  bus.attach(Pi4ioe::ADDR, &capChip);
}

void HwRig::keyboardOn() {
  _kbOn = true;
  kb.begin();
}

GpsService* HwRig::gpsOn() {
  if (!_gpsOn) {
    _gpsOn = true;
    gps.begin(_sys.millis());
    _nextSecond = _sys.millis() + 1000;
  }
  return &_gpsSvc;
}

bool HwRig::capBegin() {
  Pi4ioe io(bus);
  return io.probe() && io.setOutput(0, true);
}

bool HwRig::tapChar(char c) {
  KeyEvent e;
  e.code = KeyCode::Char;
  e.ch = c;
  return tapKey(e);
}

bool HwRig::tapKey(const KeyEvent& e) {
  Pos p{-1, -1};
  bool shift = (e.mods & MOD_SHIFT) != 0;
  bool fn = (e.mods & MOD_FN) != 0;
  switch (e.code) {
    case KeyCode::Char: {
      bool needShift = false;
      if (!findChar(e.ch, p, needShift)) return false;
      shift = shift || needShift;
      break;
    }
    case KeyCode::Enter: p = {2, 13}; break;
    case KeyCode::Backspace: p = {0, 13}; break;
    case KeyCode::Tab: p = {1, 0}; break;
    case KeyCode::Esc: p = {0, 0}; fn = true; break;
    case KeyCode::Up: p = {2, 11}; fn = true; break;
    case KeyCode::Down: p = {3, 11}; fn = true; break;
    case KeyCode::Left: p = {3, 10}; fn = true; break;
    case KeyCode::Right: p = {3, 12}; fn = true; break;
    default: return false;
  }
  Pos held[5];
  int n = 0;
  if (fn) held[n++] = FN;
  if (shift) held[n++] = SHIFT;
  if (e.mods & MOD_CTRL) held[n++] = CTRL;
  if (e.mods & MOD_ALT) held[n++] = ALT;
  if (e.mods & MOD_OPT) held[n++] = OPT;
  for (int i = 0; i < n; i++) kbChip.pressGrid(held[i].r, held[i].c, true);
  kbChip.tap(p.r, p.c);
  for (int i = n - 1; i >= 0; i--) kbChip.pressGrid(held[i].r, held[i].c, false);
  return true;
}

void HwRig::tick(uint32_t nowMs, const std::function<void(const KeyEvent&)>& sink) {
  if (_gpsOn) {
    while ((int32_t)(nowMs - _nextSecond) >= 0) {   // the module talks once a second
      uint32_t unix = _sys.nowUnix() ? _sys.nowUnix() : 1767225600u;
      if (_fix) gpsUart.sendFix(_lat, _lon, _sats, unix);
      else gpsUart.sendNoFix(unix);
      _nextSecond += 1000;
    }
    gps.loop(nowMs);
  }
  if (_kbOn) {
    KeyEvent e;
    while (kb.poll(e, nowMs)) sink(e);
  }
}
