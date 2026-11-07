#include "FloodOptions.h"
#include <string.h>

namespace floodopt {

uint8_t stepPathBytes(uint8_t n, int delta) {
  const int range = PATH_BYTES_MAX - PATH_BYTES_MIN + 1;
  int i = (validPathBytes(n) - PATH_BYTES_MIN + delta % range + range) % range;
  return (uint8_t)(i + PATH_BYTES_MIN);
}

// Same rule as RegionMap::is_name_char (meshcore/src/helpers/RegionMap.cpp)
static bool isNameChar(uint8_t c) { return c == '-' || c == '$' || c == '#' || (c >= '0' && c <= '9') || c >= 'A'; }

RegionName normalizeRegion(const char* in, char* out, size_t cap) {
  if (cap) out[0] = 0;
  if (!in) return RegionName::None;
  while (*in == ' ') in++;
  if (*in == '#') in++;
  size_t n = strlen(in);
  while (n && in[n - 1] == ' ') n--;
  if (!n) return RegionName::None;
  if (*in == '$') return RegionName::Private;
  if (n > REGION_MAX || n + 1 > cap) return RegionName::Invalid;
  for (size_t i = 0; i < n; i++) {
    if (!isNameChar((uint8_t)in[i])) return RegionName::Invalid;
  }
  memcpy(out, in, n);
  out[n] = 0;
  return RegionName::Ok;
}

}  // namespace floodopt
