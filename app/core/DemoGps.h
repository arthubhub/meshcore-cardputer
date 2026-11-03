#pragma once
// Fake GPS for the demo firmware (no Cap): a walk around Colomiers at 5 km/h.
// It writes real NMEA sentences into the NmeaParser, so the screens see exactly what a
// real module would produce: 20 s of searching, then a 3D fix.
#include "Services.h"

class DemoGps : public GpsService {
public:
  explicit DemoGps(SystemService& sys) : _sys(sys) {}
  void tick();   // call often; produces one second of NMEA per second

  const GpsState& state() override { return _parser.state(); }
  bool present() override { return true; }
  uint32_t baud() override { return 115200; }
  void coldRestart() override { _parser.reset(); _start = _sys.millis(); }

  uint32_t baseUnix = 1793541600;   // time shown by the fake GPS at start (UTC)

private:
  SystemService& _sys;
  NmeaParser _parser;
  uint32_t _start = 0;
  uint32_t _lastSecond = 0xFFFFFFFF;
};
