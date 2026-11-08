#include "HeadingFilter.h"
#include <math.h>

static float wrap360(float a) {
  a = fmodf(a, 360.0f);
  return a < 0 ? a + 360.0f : a;
}

void HeadingFilter::update(bool fix, float speedKmh, float courseDeg, uint32_t nowMs) {
  if (!fix) {
    _moving = _started = _slow = false;
    return;
  }
  if (speedKmh >= ENTER_KMH) {
    _slow = false;
    _moving = true;
  } else if (_moving && speedKmh < LEAVE_KMH) {
    if (!_slow) { _slow = true; _slowSince = nowMs; }
    if (nowMs - _slowSince >= HOLD_MS) { _moving = false; _started = false; _slow = false; }
  } else {
    _slow = false;
  }
  // The course only counts at a speed where it means something; in between, the last good one is kept
  if (!_moving || speedKmh < LEAVE_KMH) return;
  const float course = wrap360(courseDeg);
  if (!_started) {
    _heading = course;
    _started = true;
    return;
  }
  float diff = course - _heading;        // shortest way round
  if (diff > 180) diff -= 360;
  if (diff < -180) diff += 360;
  _heading = wrap360(_heading + SMOOTHING * diff);
}
