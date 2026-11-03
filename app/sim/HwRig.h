#pragma once
// "Fake hardware" mode of the simulator (layer 2 of the test pyramid):
// the real portable drivers (core/drivers) run against fake chips (fakes/), exactly as the
// firmware runs them against the real ones, and their output feeds the real UI.
//   keyboard: scenario key -> FakeTca8418 (physical taps) -> Tca8418 driver -> KeyMatrix -> App
//   GPS:      FakeGpsUart module (NMEA once a second, at its own baud) -> GpsLink -> GPS screens
//   Cap:      FakePi4ioe -> Pi4ioe driver (antenna switch)
#include "../core/Services.h"
#include "../core/drivers/GpsLink.h"
#include "../core/drivers/Pi4ioe.h"
#include "../core/drivers/Tca8418.h"
#include "SimServices.h"
#include "fakes/FakeGpsUart.h"
#include "fakes/FakeI2c.h"
#include <functional>

class SimWallClock : public WallClock {
public:
  explicit SimWallClock(SimSystem& s) : _s(s) {}
  uint32_t now() override { return _s.nowUnix(); }
  void set(uint32_t u) override { _s.st.unixTime = u - _s.virtualMillis / 1000; }

private:
  SimSystem& _s;
};

// GpsService on top of the real GpsLink driver
class HwGps : public GpsService {
public:
  HwGps(SimSystem& s, GpsLink& l) : _s(s), _l(l) {}
  const GpsState& state() override { return _l.state(); }
  bool present() override { return _l.present(_s.millis()); }
  uint32_t baud() override { return _l.baud(); }
  void coldRestart() override { _l.coldRestart(); }

private:
  SimSystem& _s;
  GpsLink& _l;
};

class HwRig {
public:
  explicit HwRig(SimSystem& sys);

  // Keyboard through the fake TCA8418 (power-on configuration by the driver).
  void keyboardOn();
  bool keyboardEnabled() const { return _kbOn; }
  // Presses the physical keys that produce `e` (modifiers held around it). False if impossible.
  bool tapKey(const KeyEvent& e);
  bool tapChar(char c);

  // GPS module through the fake UART; returns the service to plug into the UI.
  GpsService* gpsOn();
  bool gpsEnabled() const { return _gpsOn; }
  void gpsModuleFix(double lat, double lon, int sats) { _fix = true; _lat = lat; _lon = lon; _sats = sats; }
  void gpsModuleNoFix() { _fix = false; }

  // Cap expander: probe + antenna switch, like CardputerBoard::begin
  bool capBegin();

  // Called every simulated loop step: module output, driver loops, decoded keys to `sink`.
  void tick(uint32_t nowMs, const std::function<void(const KeyEvent&)>& sink);

  FakeI2cBus bus;
  FakeTca8418 kbChip;
  FakePi4ioe capChip;
  FakeGpsUart gpsUart;
  Tca8418 kb;
  GpsLink gps;

private:
  SimSystem& _sys;
  SimWallClock _clock;
  HwGps _gpsSvc;
  bool _kbOn = false, _gpsOn = false;
  bool _fix = false;
  double _lat = 0, _lon = 0;
  int _sats = 0;
  uint32_t _nextSecond = 0;
};
