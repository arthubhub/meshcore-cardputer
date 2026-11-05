#pragma once
// Radio measurements shared by both firmwares (VALIDATION-9: keyboard mode decoded far fewer packets than
// phone mode on the same hardware). Reads MeshCore's own counters - nothing in the radio path changes:
//  - every received packet and every receive error is logged with its RSSI / SNR. Errors with a strong
//    signal point at the SPI bus (the SD card shares it), errors with a weak signal are real radio losses;
//  - the heartbeat carries the counters, the noise floor, the share of time spent outside receive mode and the
//    longest gap between two loop() calls (a blocked loop delays packet reading).
// Created and attached to the board by radio_init() (target.cpp); CardputerBoard::loop() drives it.
#include <stddef.h>
#include <stdint.h>

class RadioLibWrapper;

namespace cpadv {

class RadioMonitor {
public:
  explicit RadioMonitor(RadioLibWrapper& radio) : _radio(radio) {}

  // Every loop: samples the receive mode, logs new packets and errors.
  void loop(uint32_t nowMs);
  // One-line summary for the heartbeat; starts a new measurement window (time outside Rx, loop gap).
  void summary(char* out, size_t n, uint32_t nowMs);

  uint32_t received() const { return _recv; }
  uint32_t errors() const { return _errors; }

private:
  void readSignal(float& rssi, float& snr) const;
  RadioLibWrapper& _radio;
  bool _started = false;
  uint32_t _recv = 0, _errors = 0, _sent = 0;
  uint32_t _lastEventMs = 0;
  float _lastRssi = 0, _lastSnr = 0;
  uint32_t _lastLoopMs = 0, _windowMs = 0, _notRxMs = 0, _maxGapMs = 0;
};

}  // namespace cpadv
