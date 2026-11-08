#include "RouteLayer.h"
#include <stdio.h>

using namespace theme;

namespace {
constexpr uint16_t ROUTE_COLOR = 0xF81F;   // magenta: readable on the light map and on the dark grid

double lat(const RoutePoint& p) { return p.latE6 / 1e6; }
double lon(const RoutePoint& p) { return p.lonE6 / 1e6; }
}  // namespace

bool RouteLayer::bounds(geo::Bounds& out) const {
  if (!_route) return false;
  bool any = false;
  for (int i = 0; i < _route->count; i++) {
    const RoutePoint& p = _route->points[i];
    if (!p.hasPos) continue;
    out.add(lat(p), lon(p));
    any = true;
  }
  return any;
}

void RouteLayer::draw(MapCanvas& canvas) {
  if (!_route) return;
  const ResolvedRoute& r = *_route;
  const RoutePoint* previous = nullptr;
  int previousIndex = 0;
  for (int i = 0; i < r.count; i++) {
    const RoutePoint& p = r.points[i];
    if (!p.hasPos) continue;
    if (previous) {
      const bool gap = i - previousIndex > 1;
      const bool guess = p.state == HopState::Ambiguous || previous->state == HopState::Ambiguous;
      const uint16_t color = gap ? DIM : guess ? COLOR_WARN : ROUTE_COLOR;
      canvas.line(lat(*previous), lon(*previous), lat(p), lon(p), color, gap || r.kind == PATH_PLANNED);
    }
    previous = &p;
    previousIndex = i;
  }

  lgfx::LovyanGFX& g = canvas.gfx();
  g.setFont(&fontSmall);
  g.setTextDatum(lgfx::middle_center);
  for (int i = 0; i < r.count; i++) {
    const RoutePoint& p = r.points[i];
    int x, y;
    if (!p.hasPos || !canvas.place(lat(p), lon(p), 8, x, y)) continue;
    const bool end = p.state == HopState::Endpoint;
    g.fillCircle(x, y, end ? 5 : 6, BG);
    g.fillCircle(x, y, end ? 4 : 5, p.state == HopState::Ambiguous ? COLOR_WARN : ROUTE_COLOR);
    if (end) continue;
    char n[4];
    snprintf(n, sizeof(n), "%d", i);
    g.setTextColor(BG);
    g.drawString(n, x, y);
  }
  g.setTextDatum(lgfx::top_left);
}
