#include "GpsLink.h"
#include "../Log.h"

constexpr uint32_t GpsLink::BAUDS[];

void GpsLink::tryBaud(int idx, uint32_t nowMs) {
  _baudIdx = idx % BAUD_COUNT;
  _baud = BAUDS[_baudIdx];
  _uart.open(_baud);
  _tryStart = nowMs;
  _bytesAtTry = _bytes;
  _lastSentences = _parser.state().sentences;
  CORE_LOGI("GPS", "listening at %lu baud", (unsigned long)_baud);
}

void GpsLink::begin(uint32_t nowMs) {
  _parser.reset();
  _locked = false;
  tryBaud(0, nowMs);
}

void GpsLink::coldRestart() {
  char cmd[32];
  size_t n = NmeaParser::frame("PCAS10,2", cmd, sizeof(cmd));   // CASIC cold start (AT6668 / ATGM336H)
  _uart.write((const uint8_t*)cmd, n);
  _parser.reset();
  _lastSentences = 0;
  _hadFix = false;
  _fixLogged = false;
  CORE_LOGI("GPS", "cold restart sent ($PCAS10,2)");
}

void GpsLink::loop(uint32_t nowMs) {
  int budget = BYTES_PER_LOOP;
  while (budget-- > 0 && _uart.available() > 0) {
    int b = _uart.read();
    if (b < 0) break;
    char c = (char)b;
    _bytes++;
    _parser.feed(c, nowMs);
    if (_rawEcho) {
      if (c == '\n' || c == '\r') {
        if (_rawLen) { _raw[_rawLen] = 0; CORE_LOGI("NMEA", "%s", _raw); _rawLen = 0; }
      } else if (_rawLen < sizeof(_raw) - 1) {
        _raw[_rawLen++] = (c >= 32 && c < 127) ? c : '?';
      }
    }
  }

  const GpsState& s = _parser.state();
  if (s.sentences != _lastSentences) {
    _lastSentences = s.sentences;
    _lastSentenceMs = nowMs;
    if (!_locked) {
      _locked = true;
      CORE_LOGI("GPS", "NMEA detected at %lu baud", (unsigned long)_baud);
    }
    if (_lostLogged) {
      _lostLogged = false;
      CORE_LOGI("GPS", "module talking again");
    }
  }

  if (!_locked && nowMs - _tryStart > BAUD_TRY_MS) {
    uint32_t got = _bytes - _bytesAtTry;
    if (got == 0) CORE_LOGW("GPS", "nothing received at %lu baud (module off or not wired?)", (unsigned long)_baud);
    else CORE_LOGW("GPS", "%lu bytes but no valid sentence at %lu baud (%lu checksum errors): wrong speed",
                   (unsigned long)got, (unsigned long)_baud, (unsigned long)s.checksumErrors);
    tryBaud(_baudIdx + 1, nowMs);
  }
  if (_locked && !_lostLogged && nowMs - _lastSentenceMs > SILENT_MS) {
    _lostLogged = true;
    CORE_LOGW("GPS", "no NMEA for %lu s (module silent)", (unsigned long)(SILENT_MS / 1000));
  }

  // Modules send RMC (position) before GGA (satellites, hdop): announce the fix once GGA confirms it,
  // or as "position only" when GGA still reports no satellites (weak signal indoors).
  if (s.valid != _hadFix) {
    _hadFix = s.valid;
    _fixSinceMs = nowMs;
    if (!s.valid && _fixLogged) CORE_LOGI("GPS", "fix lost");
    _fixLogged = false;   // a fix that vanished before being announced stays silent
  }
  if (s.valid && !_fixLogged) {
    if (s.ggaFix) {
      CORE_LOGI("GPS", "fix %.5f, %.5f, %u sats, hdop %.1f", s.lat, s.lon, s.sats, s.hdop);
      _fixLogged = true;
    } else if (nowMs - _fixSinceMs >= FIX_LOG_WAIT_MS) {
      CORE_LOGI("GPS", "fix %.5f, %.5f (position only: the module reports no satellites in use yet)", s.lat, s.lon);
      _fixLogged = true;
    }
  }

  // Keep the wall clock on GPS time (message timestamps, "seen x min ago").
  // First sync as soon as the time is known, then at most once a minute.
  if (_clock && s.utcUnix > 1735689600u && nowMs - _lastSentenceMs < 2000 &&
      (!_clockSynced || nowMs - _lastClockSync > 60000)) {
    uint32_t rtc = _clock->now();
    int32_t drift = (int32_t)(s.utcUnix - rtc);
    if (drift > 2 || drift < -2) {
      _clock->set(s.utcUnix);
      _clockSets++;
      CORE_LOGI("GPS", "clock set from GPS (drift %ld s)", (long)drift);
    }
    _clockSynced = true;
    _lastClockSync = nowMs;
  }
}
