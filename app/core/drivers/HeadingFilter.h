#pragma once
// Which way is up on the compass (VALIDATION-10). The Cardputer has no magnetometer: the only heading is the GPS course,
// which is noise below walking speed. Without care the dial jumps between "north up" and "my direction up" at the
// threshold, and spins while standing still.
//  - hysteresis: the course is used from ENTER_KMH, and dropped only after staying below LEAVE_KMH for HOLD_MS;
//  - smoothing: the course is averaged on the circle (359 and 1 degrees average to 0, not 180);
//  - no fix: north up at once.
#include <stdint.h>

class HeadingFilter {
public:
  static constexpr float ENTER_KMH = 4.0f;
  static constexpr float LEAVE_KMH = 2.0f;
  static constexpr uint32_t HOLD_MS = 5000;
  static constexpr float SMOOTHING = 0.35f;     // weight of a new course

  void update(bool fix, float speedKmh, float courseDeg, uint32_t nowMs);
  bool headingUp() const { return _moving; }
  float heading() const { return _moving ? _heading : 0.0f;  }   // degrees clockwise from north, 0 when north is up

private:
  bool _moving = false;
  bool _started = false;     // _heading holds a course
  float _heading = 0;
  uint32_t _slowSince = 0;
  bool _slow = false;
};
