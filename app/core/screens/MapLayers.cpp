#include "MapLayers.h"
#include <math.h>

using namespace theme;

namespace {
constexpr uint16_t MARKER = 0xFC00;    // orange
constexpr uint16_t ME = 0x041F;        // blue
constexpr int DASH_ON = 5, DASH_OFF = 4;
}  // namespace

bool MapCanvas::place(double lat, double lon, int margin, int& x, int& y) const {
  double vx, vy;
  _view.toView(lat, lon, vx, vy);
  if (vx < -margin || vx > _view.width() + margin || vy < -margin || vy > _view.height() + margin) return false;
  x = (int)lround(vx);
  y = (int)lround(vy) + _originY;
  return true;
}

void MapCanvas::line(double lat0, double lon0, double lat1, double lon1, uint16_t color, bool dashed) {
  double x0, y0, x1, y1;
  _view.toView(lat0, lon0, x0, y0);
  _view.toView(lat1, lon1, x1, y1);
  if (!_view.clip(x0, y0, x1, y1)) return;
  const double length = hypot(x1 - x0, y1 - y0);
  const double step = dashed ? DASH_ON + DASH_OFF : (length > 1 ? length : 1);
  const double on = dashed ? DASH_ON : step;
  for (double d = 0; d < length || d == 0; d += step) {
    const double e = d + on < length ? d + on : length;
    const double t0 = length > 0 ? d / length : 0, t1 = length > 0 ? e / length : 0;
    const int ax = (int)lround(x0 + (x1 - x0) * t0), ay = (int)lround(y0 + (y1 - y0) * t0) + _originY;
    const int bx = (int)lround(x0 + (x1 - x0) * t1), by = (int)lround(y0 + (y1 - y0) * t1) + _originY;
    _g.drawLine(ax, ay, bx, by, color);
    _g.drawLine(ax + 1, ay, bx + 1, by, color);
    _g.drawLine(ax, ay + 1, bx, by + 1, color);
  }
}

void ContactsLayer::draw(MapCanvas& canvas) {
  lgfx::LovyanGFX& g = canvas.gfx();
  MeshService& mesh = *canvas.app().services().mesh;
  ContactView c;
  g.setFont(&fontSmall);
  for (int i = 0; i < mesh.contactCount(); i++) {
    if (!mesh.contactAt(i, c) || !c.hasPos) continue;
    int x, y;
    if (!canvas.place(c.latE6 / 1e6, c.lonE6 / 1e6, 40, x, y)) continue;
    g.fillTriangle(x, y, x - 5, y - 10, x + 5, y - 10, c.favourite ? COLOR_WARN : MARKER);
    g.drawTriangle(x, y, x - 5, y - 10, x + 5, y - 10, BG);
    char name[12];
    utf8Copy(name, sizeof(name), c.name);
    int tw = g.textWidth(name);
    g.fillRect(x + 6, y - 12, tw + 3, 10, BG);
    g.setTextColor(FG);
    g.drawString(name, x + 7, y - 11);
  }
}

void OwnPositionLayer::draw(MapCanvas& canvas) {
  DeviceStatus st = canvas.app().services().sys->status();
  int x, y;
  if (!st.hasPosition || !canvas.place(st.lat, st.lon, 10, x, y)) return;
  lgfx::LovyanGFX& g = canvas.gfx();
  g.fillCircle(x, y, 5, FG);
  g.fillCircle(x, y, 3, ME);
  const GpsState& s = canvas.app().services().gps->state();
  if (s.valid && s.speedKmh >= 3.0f) {   // heading tick when moving
    double a = s.courseDeg * geo::DEG;
    g.drawLine(x, y, x + (int)lround(11 * sin(a)), y - (int)lround(11 * cos(a)), ME);
  }
}
