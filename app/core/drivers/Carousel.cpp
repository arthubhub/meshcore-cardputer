#include "Carousel.h"

void Carousel::setCount(int n) {
  _n = n < 1 ? 1 : n;
  if (_page >= _n) _page = 0;
  _from = -1;
}

void Carousel::go(int page, uint32_t nowMs) {
  if (page < 0 || page >= _n || page == _page) return;
  _dir = page > _page ? 1 : -1;
  _from = _page;
  _page = page;
  _startMs = nowMs;
}

void Carousel::slide(int delta, uint32_t nowMs) {
  if (_n < 2 || !delta) return;
  _from = _page;
  _page = ((_page + delta) % _n + _n) % _n;
  _dir = delta > 0 ? 1 : -1;
  _startMs = nowMs;
}

int Carousel::shift(uint32_t nowMs, int width) const {
  if (!sliding(nowMs)) return 0;
  // Ease out: fast at first, slows down at the end
  const float t = (float)(nowMs - _startMs) / (float)SLIDE_MS;
  const float eased = 1.0f - (1.0f - t) * (1.0f - t);
  return (int)((1.0f - eased) * (float)width) * _dir;   // the new page comes from the side it slides in from
}
