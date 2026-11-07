#include "MapProjection.h"

namespace {
enum : int { LEFT = 1, RIGHT = 2, ABOVE = 4, BELOW = 8 };
}

int MapProjection::code(double x, double y) const {
  int c = 0;
  if (x < 0) c |= LEFT;
  else if (x > _w - 1) c |= RIGHT;
  if (y < 0) c |= ABOVE;
  else if (y > _h - 1) c |= BELOW;
  return c;
}

// Cohen-Sutherland
bool MapProjection::clip(double& x0, double& y0, double& x1, double& y1) const {
  int c0 = code(x0, y0), c1 = code(x1, y1);
  for (int guard = 0; guard < 8; guard++) {
    if (!(c0 | c1)) return true;
    if (c0 & c1) return false;
    const int c = c0 ? c0 : c1;
    double x, y;
    if (c & LEFT) { x = 0; y = y0 + (y1 - y0) * (0 - x0) / (x1 - x0); }
    else if (c & RIGHT) { x = _w - 1; y = y0 + (y1 - y0) * (_w - 1 - x0) / (x1 - x0); }
    else if (c & ABOVE) { y = 0; x = x0 + (x1 - x0) * (0 - y0) / (y1 - y0); }
    else { y = _h - 1; x = x0 + (x1 - x0) * (_h - 1 - y0) / (y1 - y0); }
    if (c == c0) { x0 = x; y0 = y; c0 = code(x, y); }
    else { x1 = x; y1 = y; c1 = code(x, y); }
  }
  return false;
}
