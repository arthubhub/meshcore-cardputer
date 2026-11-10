#include "NavigationLayer.h"
#include "GpsScreens.h"
#include <stdio.h>

using namespace theme;

namespace {
constexpr uint16_t LINE_COLOR = 0x07FF;   // cyan, the colour of the compass needle
}

bool NavigationLayer::target(App& app, ContactView& out) const {
  return _target.valid() && app.services().mesh->findContact(_target, out) && out.hasPos;
}

void NavigationLayer::refresh(App& app) {
  ContactView c;
  _hasTarget = target(app, c);
  if (_hasTarget) { _tLat = c.latE6 / 1e6; _tLon = c.lonE6 / 1e6; }
  DeviceStatus st = app.services().sys->status();
  _hasMe = st.hasPosition;
  if (_hasMe) { _meLat = st.lat; _meLon = st.lon; }
}

bool NavigationLayer::bounds(geo::Bounds& out) const {
  if (_hasTarget) out.add(_tLat, _tLon);
  if (_hasMe) out.add(_meLat, _meLon);
  return _hasTarget || _hasMe;
}

void NavigationLayer::draw(MapCanvas& canvas) {
  refresh(canvas.app());
  if (!_hasTarget) return;
  lgfx::LovyanGFX& g = canvas.gfx();
  char text[40];
  if (_hasMe) {
    canvas.line(_meLat, _meLon, _tLat, _tLon, LINE_COLOR, false);
    const double d = geo::distanceM(_meLat, _meLon, _tLat, _tLon);
    const double b = geo::bearingDeg(_meLat, _meLon, _tLat, _tLon);
    if (d < 1000) snprintf(text, sizeof(text), "%.0f m  %s %.0f\xC2\xB0", d, geo::compassLabel(b), b);
    else snprintf(text, sizeof(text), "%.1f km  %s %.0f\xC2\xB0", d / 1000, geo::compassLabel(b), b);
  } else {
    snprintf(text, sizeof(text), "%s", tr("my position?", "ma position ?"));
  }
  g.setFont(&fontSmall);
  const int w = g.textWidth(text) + 6, h = 11;
  const int x = canvas.view().width() - w - 2, y = canvas.view().height() - h - 2 + CONTENT_Y;
  g.fillRect(x, y, w, h, BG);
  g.drawRect(x, y, w, h, LINE_COLOR);
  g.setTextColor(FG);
  g.drawString(text, x + 3, y + 1);
}

bool NavigationLayer::onKey(App& app, const KeyEvent& e) {
  if (!e.is('b') || !_compass) return false;
  _compass->setTarget(_target);
  app.push(_compass);
  return true;
}

const char* NavigationLayer::help() const {
  return tr("+- zoom  ;.,/ move  R frame  B compass", "+- zoom  ;.,/ bouger  R cadrer  B boussole");
}
