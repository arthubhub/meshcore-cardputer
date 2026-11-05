#include "CpadvRadioMonitor.h"
#include "CpadvLog.h"
#include <stdio.h>
#include <helpers/radiolib/RadioLibWrappers.h>

namespace cpadv {

void RadioMonitor::readSignal(float& rssi, float& snr) const {
  // Signal of the last packet (also of a packet that failed to decode): read from the SX1262 packet status.
  rssi = _radio.getLastRSSI();
  snr = _radio.getLastSNR();
}

void RadioMonitor::loop(uint32_t nowMs) {
  if (!_started) {
    _started = true;
    _recv = _radio.getPacketsRecv();
    _errors = _radio.getPacketsRecvErrors();
    _sent = _radio.getPacketsSent();
    _lastLoopMs = nowMs;
    return;
  }
  uint32_t dt = nowMs - _lastLoopMs;
  _lastLoopMs = nowMs;
  _windowMs += dt;
  if (dt > _maxGapMs) _maxGapMs = dt;
  if (!_radio.isInRecvMode()) _notRxMs += dt;

  uint32_t recv = _radio.getPacketsRecv(), errors = _radio.getPacketsRecvErrors();
  if (recv != _recv || errors != _errors) {
    readSignal(_lastRssi, _lastSnr);
    _lastEventMs = nowMs;
    if (recv != _recv) {
      CPLOG_I("RADIO", "rx #%lu: rssi %.0f dBm, snr %.1f dB (noise floor %d dBm)", (unsigned long)recv, _lastRssi,
              _lastSnr, _radio.getNoiseFloor());
    }
    if (errors != _errors) {
      CPLOG_W("RADIO", "rx error #%lu: rssi %.0f dBm, snr %.1f dB (%s)", (unsigned long)errors, _lastRssi, _lastSnr,
              _lastSnr > 5 ? "strong signal: suspect the SPI bus" : "weak signal: radio loss");
    }
    _recv = recv;
    _errors = errors;
  }
  _sent = _radio.getPacketsSent();
}

void RadioMonitor::summary(char* out, size_t n, uint32_t nowMs) {
  char last[40] = "none";
  if (_lastEventMs) {
    snprintf(last, sizeof(last), "%.0fdBm/%.1fdB %lus ago", _lastRssi, _lastSnr,
             (unsigned long)((nowMs - _lastEventMs) / 1000));
  }
  float offRx = _windowMs ? 100.0f * _notRxMs / _windowMs : 0;
  snprintf(out, n, "radio rx %lu err %lu tx %lu nf %d last %s | off-rx %.1f%% gap %lums", (unsigned long)_recv,
           (unsigned long)_errors, (unsigned long)_sent, _radio.getNoiseFloor(), last, offRx,
           (unsigned long)_maxGapMs);
  _windowMs = _notRxMs = _maxGapMs = 0;
}

}  // namespace cpadv
