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
constexpr uint16_t GRID_BG = 0x18E3;   // dark grey
constexpr uint16_t GRID_LINE = 0x2945;
constexpr uint16_t MARKER = 0xFC00;    // orange
constexpr uint16_t ME = 0x041F;        // blue
constexpr double DEFAULT_LAT = 46.6, DEFAULT_LON = 2.4;   // centre of France (no position at all)
}  // namespace

void MapScreen::centerOn(double lat, double lon) {
  geo::toPixel(lat, lon, _z, _cx, _cy);
  _hasCenter = true;
}

void MapScreen::setZoom(App& app, int z) {
  int zmin = 3, zmax = 17;
  TileSource* t = app.services().tiles;
  if (t && t->available()) t->zoomRange(zmin, zmax);
  if (z < zmin) z = zmin;
  if (z > zmax) z = zmax;
  if (z == _z) return;
  double lat, lon;
  geo::fromPixel(_cx, _cy, _z, lat, lon);
  _z = z;
  geo::toPixel(lat, lon, _z, _cx, _cy);
}

bool MapScreen::ownPixel(App& app, double& px, double& py) {
  DeviceStatus st = app.services().sys->status();
  if (!st.hasPosition) return false;
  geo::toPixel(st.lat, st.lon, _z, px, py);
  return true;
}

void MapScreen::onEnter(App& app) {
  TileSource* t = app.services().tiles;
  int zmin = 3, zmax = 17;
  bool tiles = t && t->available() && t->zoomRange(zmin, zmax);
  if (!_hasCenter) {
    _z = tiles ? (zmin + zmax + 1) / 2 : 13;
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
      if (!found) { _z = 5; centerOn(DEFAULT_LAT, DEFAULT_LON); }
    }
  }
  if (_follow) {
    double px, py;
    if (ownPixel(app, px, py)) { _cx = px; _cy = py; }
  }
  setZoom(app, _z);   // clamp to the available levels
}

bool MapScreen::onKey(App& app, const KeyEvent& e) {
  switch (e.code) {
    case KeyCode::Up: _cy -= PAN_STEP / 2; _follow = false; return true;
    case KeyCode::Down: _cy += PAN_STEP / 2; _follow = false; return true;
    case KeyCode::Left: _cx -= PAN_STEP; _follow = false; return true;
    case KeyCode::Right: _cx += PAN_STEP; _follow = false; return true;
    default: break;
  }
  if (e.is('=') || e.is('+')) { setZoom(app, _z + 1); return true; }
  if (e.is('-') || e.is('_')) { setZoom(app, _z - 1); return true; }
  if (e.is('0')) {
    double px, py;
    if (ownPixel(app, px, py)) {
      _cx = px;
      _cy = py;
      _follow = true;
    } else {
      app.notify("Position inconnue", "Pas encore de fix GPS", 2000);
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
  if (_follow) { _cx = px; _cy = py; }
  return true;
}

void MapScreen::drawTiles(App& app, lgfx::LovyanGFX& g, double left, double top, int& missing) {
  TileSource* t = app.services().tiles;
  static uint16_t line[MAP_W];   // one screen row (480 bytes)
  const int tiles = 1 << _z;
  const int x0 = (int)floor(left), y0 = (int)floor(top);
  const int tx0 = (int)floor(x0 / 256.0), ty0 = (int)floor(y0 / 256.0);
  const int tx1 = (int)floor((x0 + MAP_W - 1) / 256.0), ty1 = (int)floor((y0 + MAP_H - 1) / 256.0);
  missing = 0;
  for (int ty = ty0; ty <= ty1; ty++) {
    for (int tx = tx0; tx <= tx1; tx++) {
      // visible part of this tile, in tile and screen coordinates
      int sx = tx * 256 - x0, sy = ty * 256 - y0;
      int cx0 = sx < 0 ? -sx : 0, cy0 = sy < 0 ? -sy : 0;
      int cx1 = sx + 256 > MAP_W ? MAP_W - sx : 256, cy1 = sy + 256 > MAP_H ? MAP_H - sy : 256;
      int w = cx1 - cx0;
      bool ok = ty >= 0 && ty < tiles && t && t->available() && t->beginTile(_z, ((tx % tiles) + tiles) % tiles, ty);
      if (ok) {
        for (int r = cy0; r < cy1; r++) {
          if (!t->readRow(r, cx0, w, line)) { ok = false; break; }
          g.pushImage(sx + cx0, CONTENT_Y + sy + r, w, 1, (const lgfx::swap565_t*)line);
        }
        t->endTile();
      }
      if (!ok) {
        missing++;
        g.fillRect(sx + cx0, CONTENT_Y + sy + cy0, w, cy1 - cy0, GRID_BG);
        // 64 px grid so that panning stays readable without tiles
        for (int gx = (cx0 + 63) / 64 * 64; gx < cx1; gx += 64) g.drawFastVLine(sx + gx, CONTENT_Y + sy + cy0, cy1 - cy0, GRID_LINE);
        for (int gy = (cy0 + 63) / 64 * 64; gy < cy1; gy += 64) g.drawFastHLine(sx + cx0, CONTENT_Y + sy + gy, w, GRID_LINE);
      }
      app.services().sys->yield();   // keep the radio served between tiles
    }
  }
}

void MapScreen::drawMarkers(App& app, lgfx::LovyanGFX& g, double left, double top) {
  MeshService& mesh = *app.services().mesh;
  ContactView c;
  g.setFont(&fontSmall);
  for (int i = 0; i < mesh.contactCount(); i++) {
    if (!mesh.contactAt(i, c) || !c.hasPos) continue;
    double px, py;
    geo::toPixel(c.latE6 / 1e6, c.lonE6 / 1e6, _z, px, py);
    int x = (int)lround(px - left), y = (int)lround(py - top) + CONTENT_Y;
    if (x < -40 || x > MAP_W + 40 || y < CONTENT_Y - 10 || y > CONTENT_Y + MAP_H + 10) continue;
    g.fillTriangle(x, y, x - 5, y - 10, x + 5, y - 10, c.favourite ? COLOR_WARN : MARKER);
    g.drawTriangle(x, y, x - 5, y - 10, x + 5, y - 10, BG);
    char name[12];
    utf8Copy(name, sizeof(name), c.name);
    int tw = g.textWidth(name);
    g.fillRect(x + 6, y - 12, tw + 3, 10, BG);
    g.setTextColor(FG);
    g.drawString(name, x + 7, y - 11);
  }
  double px, py;
  if (ownPixel(app, px, py)) {
    int x = (int)lround(px - left), y = (int)lround(py - top) + CONTENT_Y;
    _lastOwnX = px;
    _lastOwnY = py;
    g.fillCircle(x, y, 5, FG);
    g.fillCircle(x, y, 3, ME);
    const GpsState& s = app.services().gps->state();
    if (s.valid && s.speedKmh >= 3.0f) {   // heading tick when moving
      double a = s.courseDeg * geo::DEG;
      g.drawLine(x, y, x + (int)lround(11 * sin(a)), y - (int)lround(11 * cos(a)), ME);
    }
  }
}

void MapScreen::drawScale(App& app, lgfx::LovyanGFX& g) {
  double lat, lon;
  geo::fromPixel(_cx, _cy, _z, lat, lon);
  double mpp = geo::metersPerPixel(lat, _z);
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
  snprintf(buf, sizeof(buf), "z%d%s", _z, _follow ? "" : " libre");
  tw = g.textWidth(buf);
  g.fillRect(MAP_W - tw - 6, CONTENT_Y + 1, tw + 5, 10, BG);
  g.setTextColor(_follow ? DIM : COLOR_WARN);
  g.drawString(buf, MAP_W - tw - 3, CONTENT_Y + 1);
}

void MapScreen::draw(App& app, lgfx::LovyanGFX& g) {
  uint32_t t0 = app.now();
  const double left = _cx - MAP_W / 2.0, top = _cy - MAP_H / 2.0;
  int missing = 0;
  drawTiles(app, g, left, top, missing);
  drawMarkers(app, g, left, top);
  drawScale(app, g);
  TileSource* t = app.services().tiles;
  if (!t || !t->available()) {
    g.setFont(&fontSmall);
    g.setTextColor(COLOR_WARN);
    g.setTextDatum(lgfx::top_center);
    g.drawString("Pas de carte sur la SD (dossier /tiles)", MAP_W / 2, CONTENT_Y + 14);
    g.setTextDatum(lgfx::top_left);
  } else if (missing) {
    g.setFont(&fontSmall);
    g.setTextColor(DIM);
    g.drawString("hors zone", 4, CONTENT_Y + 1);
  }
  _renderMs = app.now() - t0;
}
