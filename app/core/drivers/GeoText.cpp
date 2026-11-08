#include "GeoText.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace geotext {
namespace {

// A decimal number at p: optional sign, digits, optional fraction. `decimals` counts the digits after the point.
bool number(const char*& p, double& v, int& decimals) {
  const char* s = p;
  if (*s == '+' || *s == '-') s++;
  if (!isdigit((unsigned char)*s)) return false;
  while (isdigit((unsigned char)*s)) s++;
  decimals = 0;
  if (*s == '.') {
    s++;
    while (isdigit((unsigned char)s[decimals])) decimals++;
    s += decimals;
  }
  v = strtod(p, nullptr);
  p = s;
  return true;
}

bool plausible(double lat, double lon) {
  return fabs(lat) <= 90 && fabs(lon) <= 180 && !(lat == 0 && lon == 0);
}

// "lat,lon" or "lat, lon" at p
bool pair(const char* p, double& lat, double& lon, int minDecimals) {
  int dLat = 0, dLon = 0;
  if (!number(p, lat, dLat)) return false;
  if (*p != ',') return false;
  p++;
  while (*p == ' ') p++;
  if (!number(p, lon, dLon)) return false;
  if (isdigit((unsigned char)*p) || *p == '.') return false;
  return dLat >= minDecimals && dLon >= minDecimals && plausible(lat, lon);
}

// After a marker: "geo:", "q=", "ll=", "@"
bool afterMarker(const char* text, const char* marker, double& lat, double& lon) {
  for (const char* p = strstr(text, marker); p; p = strstr(p + 1, marker)) {
    if (pair(p + strlen(marker), lat, lon, 0)) return true;
  }
  return false;
}

bool osm(const char* text, double& lat, double& lon) {
  const char* a = strstr(text, "mlat=");
  const char* b = strstr(text, "mlon=");
  if (!a || !b) return false;
  a += 5;
  b += 5;
  int d;
  return number(a, lat, d) && number(b, lon, d) && plausible(lat, lon);
}

bool bare(const char* text, double& lat, double& lon) {
  for (const char* p = text; *p; p++) {
    const bool start = (isdigit((unsigned char)*p) || *p == '-' || *p == '+') &&
                       (p == text || !(isdigit((unsigned char)p[-1]) || p[-1] == '.' || isalpha((unsigned char)p[-1])));
    if (start && pair(p, lat, lon, MIN_BARE_DECIMALS)) return true;
  }
  return false;
}

}  // namespace

bool find(const char* text, double& lat, double& lon) {
  if (!text) return false;
  return afterMarker(text, "geo:", lat, lon) || afterMarker(text, "q=", lat, lon) || afterMarker(text, "ll=", lat, lon) ||
         afterMarker(text, "/@", lat, lon) || osm(text, lat, lon) || bare(text, lat, lon);
}

void format(double lat, double lon, char* out, size_t n) { snprintf(out, n, "geo:%.6f,%.6f", lat, lon); }

}  // namespace geotext
