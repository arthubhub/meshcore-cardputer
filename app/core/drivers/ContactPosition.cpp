#include "ContactPosition.h"

bool ContactPosition::offer(int32_t lat, int32_t lon, uint32_t when, PositionSource from) {
  if (lat == 0 && lon == 0) return false;
  if (known() && time != 0 && when < time) return false;
  latE6 = lat;
  lonE6 = lon;
  time = when;
  source = from;
  return true;
}
