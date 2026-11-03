#pragma once
// Translates raw TCA8418 key events of the Cardputer ADV into KeyEvents.
// Pure logic (no I2C), so the simulator can test it. The I2C side is device/Tca8418Keyboard.
//
// Layout and remapping follow M5Stack's M5Cardputer library (MIT): the TCA8418 is configured
// as a 7x8 matrix and its events are remapped to the 4x14 grid of the original Cardputer.
#include "Keys.h"

class KeyMatrix {
public:
  static constexpr int ROWS = 4;
  static constexpr int COLS = 14;

  // `tcaEvent` is a byte read from the TCA8418 KEY_EVENT_A register (bit 7 = pressed,
  // bits 6..0 = key number 1..80). Returns true and fills `out` when the event produces a key
  // press (modifier keys and releases only update the internal state).
  bool feed(uint8_t tcaEvent, KeyEvent& out);

  uint8_t mods() const { return _mods; }
  void reset() { _mods = 0; }

  // Grid position of a TCA8418 key number (1..80). Returns false if out of range.
  static bool toGrid(uint8_t keyNumber, int& row, int& col);
  // Inverse, for tests: TCA8418 key number for a grid position (0 if none).
  static uint8_t fromGrid(int row, int col);

private:
  uint8_t _mods = 0;
};
