#pragma once
// GPS of the Cap LoRa-1262 (ATGM336H / AT6668, NMEA over a UART).
// Portable driver: finds the baud rate by itself, parses NMEA (core/Nmea), keeps the wall clock
// on GPS time. Runs on the Cardputer (Serial1) and against sim/fakes/FakeGpsUart.
#include "../Nmea.h"
#include "../hal/Hal.h"

class GpsLink {
public:
  static constexpr uint32_t BAUDS[] = {115200, 9600, 38400};   // Cap default first
  static constexpr int BAUD_COUNT = 3;
  static constexpr uint32_t BAUD_TRY_MS = 2500;
  static constexpr uint32_t SILENT_MS = 5000;     // no sentence for this long = module lost
  static constexpr int BYTES_PER_LOOP = 512;      // never starve the radio
  static constexpr uint32_t FIX_LOG_WAIT_MS = 2000;  // wait this long for GGA before logging an RMC-only fix

  GpsLink(UartPort& uart, WallClock* clock) : _uart(uart), _clock(clock) {}

  void begin(uint32_t nowMs);
  void loop(uint32_t nowMs);
  void coldRestart();

  const GpsState& state() const { return _parser.state(); }
  bool present(uint32_t nowMs) const { return _locked && nowMs - _lastSentenceMs < SILENT_MS; }
  uint32_t baud() const { return _locked ? _baud : 0; }
  uint32_t tryingBaud() const { return _baud; }
  bool searching() const { return !_locked; }
  uint32_t bytesReceived() const { return _bytes; }
  uint32_t clockSets() const { return _clockSets; }
  void setRawEcho(bool on) { _rawEcho = on; _rawLen = 0; }

private:
  void tryBaud(int idx, uint32_t nowMs);
  UartPort& _uart;
  WallClock* _clock;
  NmeaParser _parser;
  uint32_t _baud = 0;
  int _baudIdx = 0;
  bool _locked = false;
  bool _lostLogged = false;
  uint32_t _tryStart = 0;
  uint32_t _bytesAtTry = 0;
  uint32_t _lastSentenceMs = 0;
  uint32_t _lastSentences = 0;
  uint32_t _lastClockSync = 0;
  bool _clockSynced = false;
  bool _hadFix = false;
  bool _fixLogged = false;         // the current fix was announced in the log
  uint32_t _fixSinceMs = 0;
  uint32_t _bytes = 0, _clockSets = 0;
  bool _rawEcho = false;
  char _raw[100];
  uint8_t _rawLen = 0;
};
