#include "MapScreen.h"
#include "../Geo.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
constexpr int MAP_W = SCREEN_W;
constexpr int MAP_H = CONTENT_H;
constexpr int PAN_STEP = 80;
constexpr int FRAME_MARGIN = 16;       // around a framed overlay: room for its markers
constexpr int FRAME_MAX_ZOOM = 15;     // framing a single point should still show where it is
constexpr uint16_t GRID_BG = 0x18E3;   // dark grey
constexpr uint16_t GRID_LINE = 0x2945;
constexpr double DEFAULT_LAT = 46.6, DEFAULT_LON = 2.4;   // centre of France (no position at all)
}  // namespace

// Rows of a tile go straight to the canvas
class CanvasRows : public RowSink {
public:
  CanvasRows(lgfx::LovyanGFX& g, int originY) : _g(g), _originY(originY) {}
  void row(int x, int y, int w, const uint16_t* pixels) override {
    _g.pushImage(x, _originY + y, w, 1, (const lgfx::swap565_t*)pixels);
  }
private:
  lgfx::LovyanGFX& _g;
  int _originY;
};

MapScreen::MapScreen() : _grid(MAP_W, MAP_H) {}

const char* MapScreen::help() const {
  if (_overlay) return tr("+- zoom  ;.,/ move  R frame  ` back", "+- zoom  ;.,/ déplacer  R cadrer  ` retour");
  return tr("+- zoom  ;.,/ move  0 centre on me", "+- zoom  ;.,/ déplacer  0 me centrer");
}

void MapScreen::syncLevels(App& app) {
  TileSource* t = app.services().tiles;
  int zmin, zmax;
  if (t && t->available() && t->zoomRange(zmin, zmax)) _grid.setLevels(zmin, zmax);
  else _grid.setNoLevels();
}

void MapScreen::centerOn(double lat, double lon) {
  geo::toPixel(lat, lon, _view.z, _view.cx, _view.cy);
  _hasCenter = true;
}

void MapScreen::setZoom(App& app, int z) {
  syncLevels(app);
  z = _grid.clampZoom(z);
  if (z == _view.z) return;
  double lat, lon;
  geo::fromPixel(_view.cx, _view.cy, _view.z, lat, lon);
  _view.z = z;
  geo::toPixel(lat, lon, _view.z, _view.cx, _view.cy);
}

bool MapScreen::ownPixel(App& app, double& px, double& py) {
  DeviceStatus st = app.services().sys->status();
  if (!st.hasPosition) return false;
  geo::toPixel(st.lat, st.lon, _view.z, px, py);
  return true;
}

void MapScreen::onEnter(App& app) {
  syncLevels(app);
  if (_overlay && _frameOverlay) {
    _beforeOverlay = _view;
    _hadCenterBeforeOverlay = _hasCenter;   // opened for the first time here: there is no view to come back to
    _frameOverlay = false;
    frameOverlay(app);
    return;
  }
  TileSource* t = app.services().tiles;
  int zmin = TileGrid::MIN_ZOOM, zmax = TileGrid::NO_TILES_MAX_ZOOM;
  bool tiles = t && t->available() && t->zoomRange(zmin, zmax);
  if (!_hasCenter) {
    _view.z = tiles ? (zmin + zmax + 1) / 2 : 13;
    DeviceStatus st = app.services().sys->status();
    if (st.hasPosition) {
      centerOn(st.lat, st.lon);
    } else {
      // No GPS yet: first contact with a position, else a default view
      MeshService& mesh = *app.services().mesh;
      ContactView c;
      bool found = false;
      for (int i = 0; i < mesh.contactCount() && !found; i++) {
        if (mesh.contactAt(i, c) && c.hasPos) { centerOn(c.latE6 / 1e6, c.lonE6 / 1e6); found = true; }
      }
      double hl, ho;
      if (!found && tiles && t->hint(hl, ho)) { centerOn(hl, ho); found = true; }
      if (!found) { _view.z = 5; centerOn(DEFAULT_LAT, DEFAULT_LON); }
    }
  }
  if (_view.follow) {
    double px, py;
    if (ownPixel(app, px, py)) { _view.cx = px; _view.cy = py; }
  }
  setZoom(app, _view.z);   // clamp to the available levels
}

void MapScreen::frameOverlay(App& app) {
  geo::Bounds b;
  if (!_overlay || !_overlay->bounds(b) || b.empty()) return;
  _view.follow = false;
  const int top = _grid.maxZoom() < FRAME_MAX_ZOOM ? _grid.maxZoom() : FRAME_MAX_ZOOM;
  _view.z = b.fitZoom(MAP_W, MAP_H, FRAME_MARGIN, TileGrid::MIN_ZOOM, top);
  double lat, lon;
  b.center(lat, lon);
  centerOn(lat, lon);
}

void MapScreen::leaveOverlay() {
  _view = _beforeOverlay;
  _hasCenter = _hadCenterBeforeOverlay;
  _overlay = nullptr;
}

bool MapScreen::onKey(App& app, const KeyEvent& e) {
  switch (e.code) {
    case KeyCode::Up: _view.cy -= PAN_STEP / 2; _view.follow = false; return true;
    case KeyCode::Down: _view.cy += PAN_STEP / 2; _view.follow = false; return true;
    case KeyCode::Left: _view.cx -= PAN_STEP; _view.follow = false; return true;
    case KeyCode::Right: _view.cx += PAN_STEP; _view.follow = false; return true;
    case KeyCode::Esc:
      if (_overlay) leaveOverlay();
      return false;       // App closes the screen
    default: break;
  }
  if (e.is('=') || e.is('+')) { setZoom(app, _view.z + 1); return true; }
  if (e.is('-') || e.is('_')) { setZoom(app, _view.z - 1); return true; }
  if (_overlay && e.is('r')) { frameOverlay(app); return true; }
  if (e.is('0')) {
    double px, py;
    if (ownPixel(app, px, py)) {
      _view.cx = px;
      _view.cy = py;
      _view.follow = true;
    } else {
      app.notify(tr("Position unknown", "Position inconnue"), tr("No GPS fix yet", "Pas encore de fix GPS"), 2000);
    }
    return true;
  }
  return false;
}

bool MapScreen::tick(App& app) {
  // Redraw only when our marker moved on screen (tiles are re-read from the SD card)
  if (app.now() - _lastCheck < 1000) return false;
  _lastCheck = app.now();
  double px, py;
  if (!ownPixel(app, px, py)) return false;
  bool moved = fabs(px - _lastOwnX) >= 2 || fabs(py - _lastOwnY) >= 2;
  if (!moved) return false;
  if (_view.follow) { _view.cx = px; _view.cy = py; }
  return true;
}

void MapScreen::drawTiles(App& app, lgfx::LovyanGFX& g, double left, double top, int& missing) {
  TileSource* t = app.services().tiles;
  CanvasRows rows(g, CONTENT_Y);
  missing = 0;
  syncLevels(app);
  _grid.begin(left, top, _view.z);
  TileBlit b;
  while (_grid.next(b)) {
    if (!t || !_raster.draw(*t, b, rows)) {
      missing++;
      const int y = CONTENT_Y + b.screenY;
      g.fillRect(b.screenX, y, b.w, b.h, GRID_BG);
      // 64 px grid so that panning stays readable without tiles
      for (int gx = (b.localX + 63) / 64 * 64; gx < b.localX + b.w; gx += 64) g.drawFastVLine(b.screenX + gx - b.localX, y, b.h, GRID_LINE);
      for (int gy = (b.localY + 63) / 64 * 64; gy < b.localY + b.h; gy += 64) g.drawFastHLine(b.screenX, y + gy - b.localY, b.w, GRID_LINE);
    }
    app.services().sys->yield();   // keep the radio served between tiles
  }
}

void MapScreen::drawScale(App& app, lgfx::LovyanGFX& g) {
  double lat, lon;
  geo::fromPixel(_view.cx, _view.cy, _view.z, lat, lon);
  double mpp = geo::metersPerPixel(lat, _view.z);
  static const int NICE[] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000};
  int meters = NICE[0];
  for (int n : NICE) if (n / mpp <= 70) meters = n;
  int len = (int)lround(meters / mpp);
  const int x = 4, y = CONTENT_Y + MAP_H - 6;
  char buf[24];
  if (meters >= 1000) snprintf(buf, sizeof(buf), "%d km", meters / 1000);
  else snprintf(buf, sizeof(buf), "%d m", meters);
  g.setFont(&fontSmall);
  int tw = g.textWidth(buf);
  g.fillRect(x - 2, y - 12, (len > tw ? len : tw) + 6, 16, BG);
  g.drawFastHLine(x, y, len, FG);
  g.drawFastVLine(x, y - 3, 4, FG);
  g.drawFastVLine(x + len, y - 3, 4, FG);
  g.setTextColor(FG);
  g.drawString(buf, x, y - 11);
  // zoom level, top right
  // the scale of the tiles: x2 = enlarged, /2 = reduced
  const int shift = _grid.shiftAt(_view.z);
  char scale[8] = "";
  if (shift > 0) snprintf(scale, sizeof(scale), " x%d", 1 << shift);
  else if (shift < 0) snprintf(scale, sizeof(scale), " /%d", 1 << -shift);
  snprintf(buf, sizeof(buf), "z%d%s%s", _view.z, scale, _view.follow ? "" : tr(" free", " libre"));
  tw = g.textWidth(buf);
  g.fillRect(MAP_W - tw - 6, CONTENT_Y + 1, tw + 5, 10, BG);
  g.setTextColor(_view.follow ? DIM : COLOR_WARN);
  g.drawString(buf, MAP_W - tw - 3, CONTENT_Y + 1);
}

void MapScreen::draw(App& app, lgfx::LovyanGFX& g) {
  uint32_t t0 = app.now();
  const double left = _view.cx - MAP_W / 2.0, top = _view.cy - MAP_H / 2.0;
  int missing = 0;
  drawTiles(app, g, left, top, missing);
  MapCanvas canvas(app, g, MapProjection(_view.z, left, top, MAP_W, MAP_H), CONTENT_Y);
  _contacts.draw(canvas);
  if (_overlay) _overlay->draw(canvas);
  _own.draw(canvas);
  ownPixel(app, _lastOwnX, _lastOwnY);    // tick() redraws when our marker moves away from here
  drawScale(app, g);
  TileSource* t = app.services().tiles;
  if (!t || !t->available()) {
    g.setFont(&fontSmall);
    g.setTextColor(COLOR_WARN);
    g.setTextDatum(lgfx::top_center);
    g.drawString(tr("No map on the SD card (/tiles folder)", "Pas de carte sur la SD (dossier /tiles)"), MAP_W / 2, CONTENT_Y + 14);
    g.setTextDatum(lgfx::top_left);
  } else if (missing) {
    g.setFont(&fontSmall);
    g.setTextColor(DIM);
    g.drawString(tr("outside the map", "hors zone"), 4, CONTENT_Y + 1);
  }
  _renderMs = app.now() - t0;
}
