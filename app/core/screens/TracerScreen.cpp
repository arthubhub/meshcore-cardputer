#include "TracerScreen.h"
#include "../Geo.h"
#include "../Widgets.h"
#include "../drivers/NodeLookup.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
constexpr int AREA_TOP = CONTENT_Y + 2;
constexpr int AREA_BOTTOM = CONTENT_Y + CONTENT_H - 24;   // room for the info lines
constexpr int AREA_LEFT = 6, AREA_RIGHT = SCREEN_W - 6;

void idText(const TrafficTracer::Node& n, char* out, size_t len) {
  if (n.kind == TrafficTracer::Kind::Me) {
    snprintf(out, len, "%s", tr("me", "moi"));
    return;
  }
  size_t o = 0;
  for (int i = 0; i < n.idBytes && o + 3 <= len; i++) o += (size_t)snprintf(out + o, len - o, "%02x", n.id[i]);
}

void label(const TrafficTracer::Node& n, char* out, size_t len) {
  if (n.kind == TrafficTracer::Kind::Me) snprintf(out, len, "%s", tr("me", "moi"));
  else if (n.lookup == TrafficTracer::Lookup::Known) snprintf(out, len, "%s", n.name);
  else if (n.lookup == TrafficTracer::Lookup::Ambiguous) snprintf(out, len, "%s?", n.name);
  else idText(n, out, len);
}
}  // namespace

void TracerScreen::onEnter(App& app) {
  _seenVersion = app.services().mesh->tracer().version() - 1;   // draw at once
}

void TracerScreen::sortByUse(const TrafficTracer& t) {
  _n = 0;
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
    if (t.node(i).used) _byUse[_n++] = (uint8_t)i;
  }
  for (int i = 1; i < _n; i++) {              // busiest first, the order of appearance for ties
    for (int j = i; j > 0; j--) {
      const int a = _byUse[j], b = _byUse[j - 1];
      if (t.usage(a) > t.usage(b) || (t.usage(a) == t.usage(b) && t.node(a).order < t.node(b).order)) {
        _byUse[j] = (uint8_t)b;
        _byUse[j - 1] = (uint8_t)a;
      } else {
        break;
      }
    }
  }
}

bool TracerScreen::onKey(App& app, const KeyEvent& e) {
  TrafficTracer& t = app.services().mesh->tracer();
  sortByUse(t);
  if (e.code == KeyCode::Left || e.code == KeyCode::Right) {
    if (!_n) return true;
    int pos = -1;
    for (int i = 0; i < _n; i++) {
      if (_byUse[i] == _sel) pos = i;
    }
    pos = pos < 0 ? 0 : (pos + (e.code == KeyCode::Right ? 1 : -1) + _n) % _n;
    _sel = _byUse[pos];
    return true;
  }
  if (e.is('v')) {
    _view = mapView(app) ? View::Graph : View::Map;
    app.notify(_view == View::Map ? tr("Map view", "Vue carte") : tr("Graph view", "Vue graphe"),
               _view == View::Map ? tr("Nodes by their position", "Les points selon leur position")
                                  : tr("Columns by hops to me", "Colonnes selon les sauts"), 1500);
    return true;
  }
  if (e.is('c')) {
    t.clear();
    _sel = -1;
    app.notify(tr("Tracer cleared", "Traceur effacé"), tr("Blank until the next packet", "Vide jusqu'au prochain paquet"), 1500);
    return true;
  }
  return false;
}

bool TracerScreen::tick(App& app) {
  TrafficTracer& t = app.services().mesh->tracer();
  t.advance(app.now());
  // Look a few nodes up: their names and positions (the archive lookup reads the flash: a few per frame)
  NodeLookup lookup(*app.services().mesh);
  for (int k = 0; k < LOOKUPS_PER_TICK; k++) {
    const int i = t.nextPending();
    if (i < 0) break;
    const TrafficTracer::Node& n = t.node(i);
    NodeMatch m[2];
    const int found = lookup.find(n.id, n.idBytes,
                                  n.kind == TrafficTracer::Kind::Router ? NodeLookup::Kind::Relay : NodeLookup::Kind::Any, m, 2);
    t.resolve(i, m, found);
  }
  if (_sel >= 0 && !t.node(_sel).used) _sel = -1;
  if (t.version() != _seenVersion || app.now() - _lastRedraw >= 5000) {
    _seenVersion = t.version();
    _lastRedraw = app.now();
    return true;
  }
  return false;
}

bool TracerScreen::mapView(App& app) const {
  if (_view != View::Auto) return _view == View::Map;
  const TrafficTracer& t = app.services().mesh->tracer();
  int placed = 0;
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) placed += t.node(i).used && t.node(i).hasPos;
  const GpsService* gps = app.services().gps;
  if (gps && const_cast<GpsService*>(gps)->state().valid) placed++;
  return placed >= 2;
}

void TracerScreen::layoutGraph(const TrafficTracer& t) {
  // A flow, as a traceroute reads: me on the right, one column per hop count present to the left (hops 1, 2 and 4
  // make three columns), the nodes of a column stacked in the order they appeared. Packets go left to right.
  constexpr int MAX_COLS = 5;
  bool present[256] = {};
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
    if (t.node(i).used && t.node(i).hopsFromMe) present[t.node(i).hopsFromMe] = true;
  }
  uint8_t colOf[256] = {};
  int cols = 0;
  for (int h = 1; h < 256; h++) {
    if (!present[h]) continue;
    if (cols < MAX_COLS) cols++;
    colOf[h] = (uint8_t)cols;                 // deeper than the last column: in the last one
  }
  const int right = AREA_RIGHT - 16, left = AREA_LEFT + 6;
  const float colW = cols ? (float)(right - left) / cols : 0;
  int perCol[MAX_COLS + 1] = {}, seen[MAX_COLS + 1] = {};
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
    if (t.node(i).used) perCol[colOf[t.node(i).hopsFromMe]]++;
  }
  const int top = AREA_TOP + 8, bottom = AREA_BOTTOM - 6;
  for (int pass = 0; pass < TrafficTracer::MAX_NODES; pass++) {
    int next = -1;
    for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
      if (!t.node(i).used || _x[i] != INT16_MIN) continue;
      if (next < 0 || t.node(i).order < t.node(next).order) next = i;
    }
    if (next < 0) break;
    const int col = colOf[t.node(next).hopsFromMe];
    const int n = perCol[col], k = seen[col]++;
    _x[next] = (int16_t)(right - col * colW);
    _y[next] = (int16_t)(n <= 1 ? (top + bottom) / 2 : top + (bottom - top) * k / (n - 1));
  }
}

void TracerScreen::layoutMap(App& app, const TrafficTracer& t) {
  // The placed nodes (and me, with a GPS fix) fitted to the area, north up
  double lat0 = 90, lat1 = -90, lon0 = 180, lon1 = -180;
  auto include = [&](double la, double lo) {
    if (la < lat0) lat0 = la;
    if (la > lat1) lat1 = la;
    if (lo < lon0) lon0 = lo;
    if (lo > lon1) lon1 = lo;
  };
  bool meKnown = false;
  double meLat = 0, meLon = 0;
  GpsService* gps = app.services().gps;
  if (gps && gps->state().valid) {
    meKnown = true;
    meLat = gps->state().lat;
    meLon = gps->state().lon;
    include(meLat, meLon);
  }
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
    if (t.node(i).used && t.node(i).hasPos) include(t.node(i).latE6 / 1e6, t.node(i).lonE6 / 1e6);
  }
  const double midLat = (lat0 + lat1) / 2, k = cos(midLat * 3.14159265 / 180.0);
  double spanX = (lon1 - lon0) * k, spanY = lat1 - lat0;
  if (spanX < 0.002) spanX = 0.002;           // a single place: about 200 m across
  if (spanY < 0.002) spanY = 0.002;
  const double w = AREA_RIGHT - AREA_LEFT - 40, h = AREA_BOTTOM - AREA_TOP - 22;   // room for the labels
  const double scale = fmin(w / spanX, h / spanY);   // px per degree (of latitude)
  _kmPerPx = (float)(111.2 / scale);
  const double cx = (AREA_LEFT + AREA_RIGHT) / 2.0, cy = (AREA_TOP + 6 + AREA_BOTTOM) / 2.0;
  const double clon = (lon0 + lon1) / 2, clat = midLat;
  auto project = [&](double la, double lo, int16_t& x, int16_t& y) {
    x = (int16_t)(cx + (lo - clon) * k * scale);
    y = (int16_t)(cy - (la - clat) * scale);
  };
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
    const TrafficTracer::Node& n = t.node(i);
    if (!n.used) continue;
    if (n.kind == TrafficTracer::Kind::Me && meKnown) project(meLat, meLon, _x[i], _y[i]);
    else if (n.hasPos) project(n.latE6 / 1e6, n.lonE6 / 1e6, _x[i], _y[i]);
  }
  // Without a position: between the placed nodes it is linked to, else on a line at the bottom
  int unplaced = 0;
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
    if (!t.node(i).used || _x[i] != INT16_MIN) continue;
    long sx = 0, sy = 0;
    int c = 0;
    for (int l = 0; l < TrafficTracer::MAX_LINKS; l++) {
      const TrafficTracer::Link& L = t.link(l);
      if (!L.used || (L.a != i && L.b != i)) continue;
      const int o = L.a == i ? L.b : L.a;
      if (_x[o] == INT16_MIN) continue;
      sx += _x[o];
      sy += _y[o];
      c++;
    }
    if (c) {                                   // me without a GPS fix: under the nodes it hears, clear of their labels
      const bool me = t.node(i).kind == TrafficTracer::Kind::Me;
      _x[i] = (int16_t)(sx / c + (me ? 0 : 9));
      _y[i] = (int16_t)(sy / c + (me ? 18 : -7));
    } else {
      _x[i] = (int16_t)(AREA_LEFT + 8 + (unplaced % 18) * 13);
      _y[i] = (int16_t)(AREA_BOTTOM - 4);
      unplaced++;
    }
  }
}

void TracerScreen::draw(App& app, lgfx::LovyanGFX& g) {
  TrafficTracer& t = app.services().mesh->tracer();
  if (t.me() < 0) {                            // T0: a blank map until the first packet
    g.setFont(&fontText);
    g.setTextColor(DIM);
    g.setTextDatum(lgfx::middle_center);
    g.drawString(tr("Waiting for packets...", "En attente de paquets..."), SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2 - 8);
    g.setFont(&fontSmall);
    g.drawString(tr("Each route heard is drawn here", "Chaque trajet entendu s'affiche ici"), SCREEN_W / 2,
                 CONTENT_Y + CONTENT_H / 2 + 8);
    g.setTextDatum(lgfx::top_left);
    return;
  }
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) _x[i] = _y[i] = INT16_MIN;
  const bool map = mapView(app);
  if (map) layoutMap(app, t);
  else layoutGraph(t);
  sortByUse(t);

  // A faint grid: the blank map
  for (int x = AREA_LEFT + 20; x < AREA_RIGHT; x += 40) {
    for (int y = AREA_TOP; y < AREA_BOTTOM; y += 4) g.drawPixel(x, y, BAR_BG);
  }
  for (int y = AREA_TOP + 18; y < AREA_BOTTOM; y += 36) {
    for (int x = AREA_LEFT; x < AREA_RIGHT; x += 4) g.drawPixel(x, y, BAR_BG);
  }

  // Links, then nodes (busy ones drawn last, on top)
  for (int l = 0; l < TrafficTracer::MAX_LINKS; l++) {
    const TrafficTracer::Link& L = t.link(l);
    if (!L.used || _x[L.a] == INT16_MIN || _x[L.b] == INT16_MIN) continue;
    const int u = t.linkUsage(l);
    const uint16_t c = TrafficTracer::heatColor(u);
    g.drawLine(_x[L.a], _y[L.a], _x[L.b], _y[L.b], c);
    if (u * 3 >= TrafficTracer::BUSY) g.drawLine(_x[L.a] + 1, _y[L.a], _x[L.b] + 1, _y[L.b], c);   // busy: thicker
  }
  char text[40];
  for (int k = _n - 1; k >= 0; k--) {
    const int i = _byUse[k];
    const TrafficTracer::Node& n = t.node(i);
    const int x = _x[i], y = _y[i];
    const uint16_t c = TrafficTracer::heatColor(t.usage(i));
    switch (n.kind) {
      case TrafficTracer::Kind::Me:
        g.drawCircle(x, y, 5, ACCENT);
        g.fillCircle(x, y, 2, ACCENT);
        break;
      case TrafficTracer::Kind::Router:
        if (n.lookup == TrafficTracer::Lookup::Ambiguous) g.drawRect(x - 3, y - 3, 7, 7, c);   // not sure which
        else g.fillRect(x - 3, y - 3, 7, 7, c);
        break;
      case TrafficTracer::Kind::Client:
        g.fillCircle(x, y, 3, c);
        break;
    }
    if (i == _sel) g.drawCircle(x, y, 7, FG);
    // Names of the busiest four, of me and of the selected node
    if (k < 4 || i == _sel || n.kind == TrafficTracer::Kind::Me) {
      label(n, text, sizeof(text));
      char shown[16];
      g.setFont(&fontSmall);
      fitText(g, text, 54, shown, sizeof(shown));
      g.setTextColor(i == _sel ? FG : DIM);
      if (n.kind == TrafficTracer::Kind::Me) {   // under its ring: the nodes next to me keep their labels
        g.setTextDatum(lgfx::top_center);
        g.drawString(shown, x, y + 7);
        g.setTextDatum(lgfx::top_left);
      } else {
        const int lx = x + 6 + g.textWidth(shown) > AREA_RIGHT ? x - 6 - g.textWidth(shown) : x + 6;
        g.drawString(shown, lx, y - 4);
      }
    }
  }

  // Information: the selected node, or the totals; then the scale and the colour key
  g.setFont(&fontSmall);
  const int infoY = AREA_BOTTOM + 3;
  g.setTextColor(FG);
  if (_sel >= 0) {
    const TrafficTracer::Node& n = t.node(_sel);
    char id[10];
    idText(n, id, sizeof(id));
    label(n, text, sizeof(text));
    char line[80];
    const char* kind = n.kind == TrafficTracer::Kind::Router ? tr("repeater", "routeur")
                       : n.kind == TrafficTracer::Kind::Client ? tr("sender", "émetteur") : tr("this Cardputer", "ce Cardputer");
    if (n.kind == TrafficTracer::Kind::Me)
      snprintf(line, sizeof(line), tr("This Cardputer: %d packets heard in 20 min", "Ce Cardputer : %d paquets en 20 min"),
               t.usage(_sel));
    else
      snprintf(line, sizeof(line), tr("%s (%s, %s): %d pkt / 20 min, %u hop%s", "%s (%s, %s) : %d paq. / 20 min, %u saut%s"),
               text, id, kind, t.usage(_sel), (unsigned)n.hopsFromMe, n.hopsFromMe > 1 ? "s" : "");
    char shown[64];
    fitText(g, line, SCREEN_W - 2 * MARGIN, shown, sizeof(shown));
    g.drawString(shown, MARGIN, infoY);
  } else {
    int routers = 0, clients = 0;
    for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
      if (!t.node(i).used) continue;
      routers += t.node(i).kind == TrafficTracer::Kind::Router;
      clients += t.node(i).kind == TrafficTracer::Kind::Client;
    }
    snprintf(text, sizeof(text), tr("%d repeater%s, %d sender%s, %d packet%s", "%d routeur%s, %d émetteur%s, %d paquet%s"),
             routers, routers > 1 ? "s" : "", clients, clients > 1 ? "s" : "", t.packets(), t.packets() > 1 ? "s" : "");
    g.drawString(text, MARGIN, infoY);
  }
  // Colour key: green little used .. red busy, over 20 minutes
  const int ky = infoY + 10;
  for (int i = 0; i < 40; i++) g.drawFastVLine(MARGIN + i, ky + 1, 5, TrafficTracer::heatColor(i * TrafficTracer::BUSY / 39));
  g.setTextColor(DIM);
  g.drawString(tr("quiet..busy 20min", "calme..chargé 20min"), MARGIN + 44, ky);
  // Right: the scale of the map (a 30 px bar), or what the rings of the graph mean
  g.setTextDatum(lgfx::top_right);
  if (map && _kmPerPx > 0) {
    const float km = _kmPerPx * 30;
    if (km >= 1) snprintf(text, sizeof(text), "%.0f km", km);
    else snprintf(text, sizeof(text), "%.0f m", km * 1000);
    const int tw = g.textWidth(text);
    g.drawString(text, SCREEN_W - MARGIN, ky);
    const int bx = SCREEN_W - MARGIN - tw - 34;
    g.drawFastHLine(bx, ky + 4, 30, DIM);
    g.drawFastVLine(bx, ky + 2, 5, DIM);
    g.drawFastVLine(bx + 29, ky + 2, 5, DIM);
  } else if (!map) {
    g.drawString(tr("<- hops to me", "<- sauts vers moi"), SCREEN_W - MARGIN, ky);
  }
  g.setTextDatum(lgfx::top_left);
}
