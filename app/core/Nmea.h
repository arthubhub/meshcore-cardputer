#pragma once
// Minimal NMEA 0183 parser (GGA, RMC, GSA, VTG; any talker: GP, GN, GL, GA, BD...).
// Portable and allocation-free: the Cardputer feeds it from the GPS UART, the simulator
// from scenario lines / recorded tracks. ~120 bytes of state.
#include <stdint.h>
#include <stddef.h>

struct GpsState {
  bool valid = false;         // position fix (GGA quality > 0 and RMC status A)
  double lat = 0, lon = 0;    // degrees
  float altM = 0;             // altitude above mean sea level
  uint8_t sats = 0;           // satellites used
  float hdop = 99.9f;
  float speedKmh = 0;
  float courseDeg = 0;        // course over ground, valid when moving
  uint8_t fixType = 1;        // GSA: 1 none, 2 2D, 3 3D
  uint32_t utcUnix = 0;       // date + time from RMC (0 = unknown)
  uint32_t fixMillis = 0;     // millis() of the last valid position
  uint32_t sentences = 0;     // valid sentences parsed (detects baud rate / wiring)
  uint32_t checksumErrors = 0;
};

class NmeaParser {
public:
  // Feed one received byte; `nowMs` is stored with each new fix. Returns true when a
  // sentence completed and updated the state.
  bool feed(char c, uint32_t nowMs);
  // Feed a whole sentence (with or without trailing CR/LF).
  bool feedLine(const char* line, uint32_t nowMs);
  const GpsState& state() const { return _s; }
  void reset() { _s = GpsState(); _len = 0; }

  // Builds "$BODY*CS\r\n" into `out` (for configuration commands). Returns length.
  static size_t frame(const char* body, char* out, size_t n);

private:
  bool parse(uint32_t nowMs);
  GpsState _s;
  char _buf[96];
  uint8_t _len = 0;
  bool _ggaFix = false, _rmcFix = false;
};
