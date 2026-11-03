#pragma once
// Fake GPS module behind a UART (simulator + unit tests).
// The module talks at `moduleBaud`: when the driver opened the port at the same speed it receives
// the NMEA text, otherwise it receives garbage bytes (what a wrong baud rate really looks like).
#include "../../core/hal/Hal.h"
#include <deque>
#include <string>
#include <vector>

class FakeGpsUart : public UartPort {
public:
  uint32_t moduleBaud = 115200;
  bool silent = false;               // module off / not wired
  bool corruptChecksums = false;     // line noise: every sentence has a wrong checksum

  // Sends one sentence. `body` without '$' and checksum ("GNRMC,..."), or a full "$...*hh" line.
  void send(const std::string& sentence);
  // One second of output: RMC + GGA (+GSA) for a position, or without fix.
  void sendFix(double lat, double lon, int sats, uint32_t unixTime);
  void sendNoFix(uint32_t unixTime);

  // UartPort
  void open(uint32_t baud) override { openedBaud = baud; opens++; rx.clear(); }
  int available() override { return (int)rx.size(); }
  int read() override {
    if (rx.empty()) return -1;
    int c = rx.front();
    rx.pop_front();
    return c;
  }
  size_t write(const uint8_t* d, size_t n) override { written.append((const char*)d, n); return n; }

  uint32_t openedBaud = 0;
  int opens = 0;
  std::deque<uint8_t> rx;
  std::string written;     // commands sent to the module

private:
  uint32_t _noise = 12345;
};

// Wall clock for tests.
class FakeClock : public WallClock {
public:
  uint32_t t = 0;
  int sets = 0;
  uint32_t now() override { return t; }
  void set(uint32_t u) override { t = u; sets++; }
};
