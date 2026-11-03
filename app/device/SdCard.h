#pragma once
// microSD card of the Cardputer (CS 12), on the SPI bus shared with the SX1262.
// Every card access holds sdcard::Lock; the radio driver runs in the same task, so a radio
// transfer never interleaves with a card transfer.
namespace sdcard {
bool begin();     // mounts the card once (no card = false, everything else keeps working)
bool mounted();
struct Lock {
  Lock();
  ~Lock();
};
}  // namespace sdcard
