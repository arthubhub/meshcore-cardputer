#pragma once
// Positions written in text messages (MESSAGING-11). MeshCore has no position message: people paste coordinates or a
// link. Recognised, anywhere in the text:
//   geo:43.6047,1.4442             (RFC 5870, what this firmware sends)
//   43.6047, 1.4442  /  -12.5,130.84   a pair of decimal degrees
//   ...?q=43.6047,1.4442  ...?ll=...  .../@43.6047,1.4442,15z   (map links)
//   ...?mlat=43.6047&mlon=1.4442   (OpenStreetMap)
// To avoid reading "1.5, 2.5 km" as a place, a bare pair needs at least 3 decimals on each number; a value out of range
// or 0,0 is refused.
#include <stddef.h>

namespace geotext {

constexpr int MIN_BARE_DECIMALS = 3;

bool find(const char* text, double& lat, double& lon);
// "geo:43.604700,1.444200"
void format(double lat, double lon, char* out, size_t n);

}  // namespace geotext
