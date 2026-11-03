#include "FakeGpsUart.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static std::string frame(const std::string& body, bool corrupt) {
  uint8_t cs = 0;
  for (char c : body) cs ^= (uint8_t)c;
  if (corrupt) cs ^= 0x5A;
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X\r\n", cs);
  return "$" + body + tail;
}

void FakeGpsUart::send(const std::string& sentence) {
  if (silent) return;
  std::string line;
  if (!sentence.empty() && sentence[0] == '$') {
    line = sentence;
    if (line.size() < 2 || line.substr(line.size() - 2) != "\r\n") line += "\r\n";
  } else {
    line = frame(sentence, corruptChecksums);
  }
  if (openedBaud == moduleBaud) {
    for (char c : line) rx.push_back((uint8_t)c);
  } else {
    // Wrong speed: framing errors, bytes that look random (fewer or more of them)
    size_t n = openedBaud > moduleBaud ? line.size() / 2 + 1 : line.size() * 2;
    for (size_t i = 0; i < n; i++) {
      _noise = _noise * 1103515245u + 12345u;
      rx.push_back((uint8_t)(_noise >> 16));
    }
  }
}

static void nmeaCoord(double v, bool lat, char* out, size_t n, char& hemi) {
  hemi = lat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W');
  v = fabs(v);
  int d = (int)v;
  double m = (v - d) * 60.0;
  snprintf(out, n, lat ? "%02d%07.4f" : "%03d%07.4f", d, m);
}

static void hmsDate(uint32_t unix, char* hms, char* dmy) {
  time_t t = unix;
  struct tm g;
  gmtime_r(&t, &g);
  sprintf(hms, "%02d%02d%02d.00", g.tm_hour, g.tm_min, g.tm_sec);
  sprintf(dmy, "%02d%02d%02d", g.tm_mday, g.tm_mon + 1, g.tm_year % 100);
}

void FakeGpsUart::sendFix(double lat, double lon, int sats, uint32_t unixTime) {
  char la[16], lo[16], hms[16], dmy[16], ns, ew, b[160];
  nmeaCoord(lat, true, la, sizeof(la), ns);
  nmeaCoord(lon, false, lo, sizeof(lo), ew);
  hmsDate(unixTime, hms, dmy);
  snprintf(b, sizeof(b), "GNRMC,%s,A,%s,%c,%s,%c,0.10,0.0,%s,,,A", hms, la, ns, lo, ew, dmy);
  send(b);
  snprintf(b, sizeof(b), "GNGGA,%s,%s,%c,%s,%c,1,%02d,0.9,150.0,M,49.6,M,,", hms, la, ns, lo, ew, sats);
  send(b);
  send("GNGSA,A,3,01,02,03,04,,,,,,,,,1.5,0.9,1.2,1");
}

void FakeGpsUart::sendNoFix(uint32_t unixTime) {
  char hms[16], dmy[16], b[120];
  hmsDate(unixTime, hms, dmy);
  snprintf(b, sizeof(b), "GNRMC,%s,V,,,,,,,%s,,,N", hms, dmy);
  send(b);
  snprintf(b, sizeof(b), "GNGGA,%s,,,,,0,00,99.9,,,,,,", hms);
  send(b);
}
