#include "ContactCapacity.h"

ContactCapacity::Alert ContactCapacity::poll(int count, int max) {
  if (max <= 0) return Alert::None;
  const Alert now = count >= max ? Alert::Full : near(count, max) ? Alert::Near : Alert::None;
  if (now > _level) {
    _level = now;
    return now;
  }
  if (count * 100 < max * REARM_PERCENT) _level = Alert::None;
  else if (now == Alert::Near && _level == Alert::Full) _level = Alert::Near;   // a place freed: full can be told again
  return Alert::None;
}
