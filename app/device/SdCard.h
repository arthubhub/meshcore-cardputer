#pragma once
// microSD card of the Cardputer (CS 12), on the SPI bus shared with the SX1262.
// Every card access holds sdcard::Lock; the radio driver runs in the same task, so a radio
// transfer never interleaves with a card transfer. Releasing the outermost Lock clocks one dummy byte with the
// card deselected, so the card stops driving MISO before the radio uses the bus again.
namespace sdcard {
bool begin();     // mounts the card once (no card = false, everything else keeps working)
bool mounted();
struct Lock {
  Lock();
  ~Lock();
};
}  // namespace sdcard
