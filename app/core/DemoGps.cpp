#include "DemoGps.h"
#include "Geo.h"
#include <math.h>
#include <stdio.h>

namespace {
constexpr double CENTER_LAT = 43.6116, CENTER_LON = 1.3346;   // Colomiers
constexpr double RADIUS_M = 600;
constexpr double SPEED_MS = 5.0 / 3.6;
constexpr uint32_t SEARCH_S = 20;

void dm(double v, bool lat, char* out, size_t n, char& hemi) {
  double a = fabs(v);
  int d = (int)a;
  double m = (a - d) * 60;
  snprintf(out, n, lat ? "%02d%07.4f" : "%03d%07.4f", d, m);
  hemi = lat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
}
}  // namespace

void DemoGps::tick() {
  uint32_t now = _sys.millis();
  if (_lastSecond == 0xFFFFFFFF) _start = now;
  uint32_t sec = (now - _start) / 1000;
  if (sec == _lastSecond) return;
  _lastSecond = sec;

  uint32_t t = baseUnix + sec;
  unsigned hh = t / 3600 % 24, mi = t / 60 % 60, ss = t % 60;
  // date: days since epoch -> ddmmyy (civil from days)
  int32_t z = t / 86400 + 719468;
  int32_t era = z / 146097;
  uint32_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  unsigned d = doy - (153 * mp + 2) / 5 + 1, mo = mp < 10 ? mp + 3 : mp - 9;
  unsigned yy = (yoe + era * 400 + (mo <= 2)) % 100;

  char body[96], line[112];
  if (sec < SEARCH_S) {
    snprintf(body, sizeof(body), "GNRMC,%02u%02u%02u.00,V,,,,,,,%02u%02u%02u,,,N", hh, mi, ss, d, mo, yy);
    NmeaParser::frame(body, line, sizeof(line));
    _parser.feedLine(line, now);
    snprintf(body, sizeof(body), "GNGGA,%02u%02u%02u.00,,,,,0,%02u,99.9,,,,,,", hh, mi, ss, (unsigned)(sec / 4));
    NmeaParser::frame(body, line, sizeof(line));
    _parser.feedLine(line, now);
    return;
  }
  // position on a circle around the centre, walking clockwise
  double ang = (sec - SEARCH_S) * SPEED_MS / RADIUS_M;   // radians
  double lat = CENTER_LAT + RADIUS_M * cos(ang) / 111320.0;
  double lon = CENTER_LON + RADIUS_M * sin(ang) / (111320.0 * cos(CENTER_LAT * geo::DEG));
  double course = fmod(ang / geo::DEG + 90.0, 360.0);
  char la[16], lo[16], ns, ew;
  dm(lat, true, la, sizeof(la), ns);
  dm(lon, false, lo, sizeof(lo), ew);
  unsigned sats = 6 + (sec / 7) % 6;
  snprintf(body, sizeof(body), "GNRMC,%02u%02u%02u.00,A,%s,%c,%s,%c,%.2f,%.1f,%02u%02u%02u,,,A", hh, mi, ss, la, ns, lo,
           ew, SPEED_MS * 3.6 / 1.852, course, d, mo, yy);
  NmeaParser::frame(body, line, sizeof(line));
  _parser.feedLine(line, now);
  snprintf(body, sizeof(body), "GNGGA,%02u%02u%02u.00,%s,%c,%s,%c,1,%02u,0.9,162.0,M,49.6,M,,", hh, mi, ss, la, ns, lo, ew,
           sats);
  NmeaParser::frame(body, line, sizeof(line));
  _parser.feedLine(line, now);
}
