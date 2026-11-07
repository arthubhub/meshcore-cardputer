#include "AirtimeMeter.h"

float AirtimeMeter::limitPercent(float f) {
  if (f >= 869.4f && f <= 869.65f) return 10.0f;
  if (f >= 868.7f && f < 869.2f) return 0.1f;
  if (f >= 865.0f && f < 868.6f) return 1.0f;
  if (f >= 869.7f && f <= 870.0f) return 1.0f;
  if (f >= 863.0f && f < 870.0f) return 0.1f;    // the general rule of the band
  return 0.0f;
}

void AirtimeMeter::advance(uint32_t nowMs) {
  uint32_t elapsed = nowMs - _bucketStart;
  if (elapsed < BUCKET_MS) return;
  uint32_t steps = elapsed / BUCKET_MS;
  _bucketStart += steps * BUCKET_MS;
  if (steps > BUCKETS) steps = BUCKETS;
  for (uint32_t i = 0; i < steps; i++) {
    _cur = (_cur + 1) % BUCKETS;
    _b[_cur] = 0;
  }
}

void AirtimeMeter::update(uint32_t totalTxMs, uint32_t nowMs) {
  if (!_started) {
    _started = true;
    _bucketStart = nowMs;
    _lastTotal = totalTxMs;
    return;
  }
  advance(nowMs);
  uint32_t delta = totalTxMs - _lastTotal;
  _lastTotal = totalTxMs;
  uint32_t v = _b[_cur] + delta;
  _b[_cur] = v > BUCKET_MS ? (uint16_t)BUCKET_MS : (uint16_t)v;
}

uint32_t AirtimeMeter::lastHourMs() const {
  uint32_t sum = 0;
  for (uint16_t v : _b) sum += v;
  return sum;
}

AirtimeMeter::Alert AirtimeMeter::pollAlert(float limitPct) {
  if (limitPct <= 0) return Alert::None;
  float pct = percent();
  Alert now = pct >= limitPct ? Alert::Over : pct >= limitPct * WARN_SHARE ? Alert::Near : Alert::None;
  if (now > _level) {
    _level = now;
    return now;
  }
  // Hysteresis: hovering around a threshold must not repeat the warning
  if (now == Alert::None) _level = pct < limitPct * REARM_SHARE ? Alert::None : _level == Alert::Over ? Alert::Near : _level;
  return Alert::None;
}
