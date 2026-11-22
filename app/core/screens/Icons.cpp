#include "Icons.h"

void drawNavIcon(lgfx::LovyanGFX& g, NavIcon icon, int x, int y, int s, uint16_t c) {
  auto X = [&](float u) { return x + (int)(u * s); };
  auto Y = [&](float u) { return y + (int)(u * s); };
  auto line = [&](float x0, float y0, float x1, float y1) {
    g.drawLine(X(x0), Y(y0), X(x1), Y(y1), c);
    if (s > 1) g.drawLine(X(x0) + 1, Y(y0), X(x1) + 1, Y(y1), c);   // thicker when large
  };
  switch (icon) {
    case NavIcon::HOME:
      line(1, 8, 8, 1);
      line(8, 1, 15, 8);
      g.drawRect(X(3), Y(8), 10 * s, 7 * s, c);
      g.fillRect(X(7), Y(10), 2 * s, 5 * s, c);
      break;
    case NavIcon::MESSAGES:
      g.drawRoundRect(X(1), Y(3), 14 * s, 10 * s, s, c);
      line(1, 3, 8, 9);
      line(8, 9, 15, 3);
      break;
    case NavIcon::CONTACTS:
      g.drawCircle(X(8), Y(5), 3 * s, c);
      g.drawArc(X(8), Y(16), 7 * s, 7 * s - (s > 1 ? 2 : 1), 180, 360, c);   // the shoulders: the upper half only
      break;
    case NavIcon::GPS:
      g.drawCircle(X(8), Y(8), 6 * s, c);
      g.fillTriangle(X(8), Y(3), X(6), Y(9), X(10), Y(9), c);   // the needle, north up
      g.drawLine(X(8), Y(9), X(8), Y(13), c);
      break;
    case NavIcon::MAP:
      line(1, 3, 5, 1);
      line(5, 1, 11, 3);
      line(11, 3, 15, 1);
      line(15, 1, 15, 13);
      line(15, 13, 11, 15);
      line(11, 15, 5, 13);
      line(5, 13, 1, 15);
      line(1, 15, 1, 3);
      line(5, 1, 5, 13);
      line(11, 3, 11, 15);
      break;
    case NavIcon::NEARBY:
      g.fillCircle(X(8), Y(10), 2 * s, c);
      line(8, 10, 8, 15);
      g.drawArc(X(8), Y(10), 5 * s, 5 * s - 1, 210, 330, c);
      g.drawArc(X(8), Y(10), 8 * s, 8 * s - 1, 215, 325, c);
      break;
    case NavIcon::PACKETS:
      line(1, 9, 4, 9);
      line(4, 9, 6, 3);
      line(6, 3, 9, 14);
      line(9, 14, 11, 6);
      line(11, 6, 13, 9);
      line(13, 9, 15, 9);
      break;
    case NavIcon::SETTINGS:
      g.drawCircle(X(8), Y(8), 5 * s, c);
      g.drawCircle(X(8), Y(8), 2 * s, c);
      for (int k = 0; k < 8; k++) {   // teeth
        static const float dx[8] = {0, 0.71f, 1, 0.71f, 0, -0.71f, -1, -0.71f};
        static const float dy[8] = {-1, -0.71f, 0, 0.71f, 1, 0.71f, 0, -0.71f};
        line(8 + dx[k] * 5, 8 + dy[k] * 5, 8 + dx[k] * 7, 8 + dy[k] * 7);
      }
      break;
    case NavIcon::TRACER:
      g.fillCircle(X(3), Y(12), s + 1, c);
      g.fillCircle(X(8), Y(4), s + 1, c);
      g.fillCircle(X(13), Y(11), s + 1, c);
      line(3, 12, 8, 4);
      line(8, 4, 13, 11);
      break;
  }
}
