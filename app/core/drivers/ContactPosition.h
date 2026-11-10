#pragma once
// Where a contact is, since when, and how we know (MESSAGING-11). Adverts and messages both carry positions, with the
// sender's own clock: comparing those times keeps the newest, whatever arrives last on the air.
#include <stdint.h>

enum class PositionSource : uint8_t { None = 0, Advert = 1, Message = 2 };

struct ContactPosition {
  int32_t latE6 = 0, lonE6 = 0;
  uint32_t time = 0;                      // sender's clock (unix), 0 unknown
  PositionSource source = PositionSource::None;

  bool known() const { return source != PositionSource::None || latE6 != 0 || lonE6 != 0; }
  // Takes the position if it is not older than the one held. An unknown time (0) never replaces a known one.
  bool offer(int32_t lat, int32_t lon, uint32_t when, PositionSource from);
};
