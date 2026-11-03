#include "Nmea.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Splits the comma-separated fields in place. Returns the number of fields.
int split(char* s, char* f[], int maxF) {
  int n = 0;
  f[n++] = s;
  for (char* p = s; *p && n < maxF; p++) {
    if (*p == ',') { *p = 0; f[n++] = p + 1; }
  }
  return n;
}

// "ddmm.mmmm" + hemisphere -> signed degrees
bool coord(const char* v, const char* hemi, double& out) {
  if (!*v || !*hemi) return false;
  double raw = atof(v);
  int deg = (int)(raw / 100);
  double d = deg + (raw - deg * 100) / 60.0;
  if (*hemi == 'S' || *hemi == 'W') d = -d;
  out = d;
  return true;
}

uint32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}

}  // namespace

size_t NmeaParser::frame(const char* body, char* out, size_t n) {
  uint8_t cs = 0;
  for (const char* p = body; *p; p++) cs ^= (uint8_t)*p;
  return snprintf(out, n, "$%s*%02X\r\n", body, cs);
}

bool NmeaParser::feed(char c, uint32_t nowMs) {
  if (c == '$') { _len = 0; _buf[_len++] = c; return false; }
  if (!_len) return false;
  if (c == '\r' || c == '\n') {
    _buf[_len] = 0;
    bool ok = parse(nowMs);
    _len = 0;
    return ok;
  }
  if (_len < sizeof(_buf) - 1) _buf[_len++] = c;
  else _len = 0;   // too long: not NMEA, drop
  return false;
}

bool NmeaParser::feedLine(const char* line, uint32_t nowMs) {
  bool ok = false;
  for (const char* p = line; *p; p++) ok |= feed(*p, nowMs);
  ok |= feed('\n', nowMs);
  return ok;
}

bool NmeaParser::parse(uint32_t nowMs) {
  // Checksum "*HH" is mandatory
  char* star = strrchr(_buf, '*');
  if (_buf[0] != '$' || !star || hexVal(star[1]) < 0 || hexVal(star[2]) < 0) return false;
  uint8_t cs = 0;
  for (char* p = _buf + 1; p < star; p++) cs ^= (uint8_t)*p;
  if (cs != (hexVal(star[1]) << 4 | hexVal(star[2]))) { _s.checksumErrors++; return false; }
  *star = 0;

  char* f[24];
  int n = split(_buf + 1, f, 24);
  if (strlen(f[0]) != 5) return false;
  const char* type = f[0] + 2;   // skip the talker (GP, GN...)
  bool updated = false;

  if (!strcmp(type, "GGA") && n >= 10) {
    int quality = atoi(f[6]);
    _ggaFix = quality > 0;
    _s.sats = (uint8_t)atoi(f[7]);
    if (*f[8]) _s.hdop = (float)atof(f[8]);
    if (_ggaFix) {
      double lat, lon;
      if (coord(f[2], f[3], lat) && coord(f[4], f[5], lon)) { _s.lat = lat; _s.lon = lon; }
      if (*f[9]) _s.altM = (float)atof(f[9]);
      _s.fixMillis = nowMs;
    }
    updated = true;
  } else if (!strcmp(type, "RMC") && n >= 10) {
    _rmcFix = *f[2] == 'A';
    if (_rmcFix) {
      double lat, lon;
      if (coord(f[3], f[4], lat) && coord(f[5], f[6], lon)) { _s.lat = lat; _s.lon = lon; }
      _s.speedKmh = *f[7] ? (float)(atof(f[7]) * 1.852) : 0;
      if (*f[8]) _s.courseDeg = (float)atof(f[8]);
      _s.fixMillis = nowMs;
    }
    // hhmmss(.ss) + ddmmyy -> unix time (also valid before the position fix)
    const char* t = f[1];
    const char* d = f[9];
    if (strlen(t) >= 6 && strlen(d) == 6) {
      int hh = (t[0] - '0') * 10 + t[1] - '0', mi = (t[2] - '0') * 10 + t[3] - '0', ss = (t[4] - '0') * 10 + t[5] - '0';
      int dd = (d[0] - '0') * 10 + d[1] - '0', mo = (d[2] - '0') * 10 + d[3] - '0', yy = (d[4] - '0') * 10 + d[5] - '0';
      if (mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31 && yy >= 24) {
        _s.utcUnix = daysFromCivil(2000 + yy, mo, dd) * 86400u + hh * 3600 + mi * 60 + ss;
      }
    }
    updated = true;
  } else if (!strcmp(type, "GSA") && n >= 3) {
    int ft = atoi(f[2]);
    if (ft >= 1 && ft <= 3) _s.fixType = ft;
    updated = true;
  } else if (!strcmp(type, "VTG") && n >= 8) {
    if (*f[1]) _s.courseDeg = (float)atof(f[1]);
    if (*f[7]) _s.speedKmh = (float)atof(f[7]);
    updated = true;
  } else {
    return false;   // other sentences (GSV, TXT...) are ignored
  }
  _s.valid = _ggaFix || _rmcFix;
  _s.sentences++;
  return updated;
}
