#pragma once
// The pages of the home screen (navigation menu): which page is shown, and the slide from one page to the next.
// Portable and unit-tested; the screen draws the pages at the offsets it gives.
#include <stdint.h>

class Carousel {
public:
  static constexpr uint32_t SLIDE_MS = 160;

  void setCount(int n);
  int count() const { return _n; }
  int page() const { return _page; }            // the page shown (or arriving)
  void go(int page, uint32_t nowMs);            // straight to a page, with a slide
  void slide(int delta, uint32_t nowMs);        // +1 right, -1 left; wraps around
  bool sliding(uint32_t nowMs) const { return _from >= 0 && nowMs - _startMs < SLIDE_MS; }
  // During a slide: the page leaving, and the horizontal shift of the arriving page in pixels for a screen `width`
  // wide (the leaving page is drawn `width` further, on the side it leaves to). Outside a slide: from = -1, shift 0.
  int leaving(uint32_t nowMs) const { return sliding(nowMs) ? _from : -1; }
  int shift(uint32_t nowMs, int width) const;

private:
  int _n = 1, _page = 0, _from = -1, _dir = 1;
  uint32_t _startMs = 0;
};
