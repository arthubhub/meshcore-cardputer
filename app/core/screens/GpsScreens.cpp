#include "GpsScreens.h"
#include "../Geo.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {

void formatDistance(double m, char* out, size_t n) {
  if (m < 1000) snprintf(out, n, "%.0f m", m);
  else if (m < 10000) snprintf(out, n, "%.2f km", m / 1000);
  else snprintf(out, n, "%.1f km", m / 1000);
}

void formatCoord(double v, bool lat, char* out, size_t n) {
  char hemi = lat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'O');
  snprintf(out, n, "%.5f\xC2\xB0 %c", fabs(v), hemi);
}

}  // namespace

// =============================================================== GpsScreen

bool GpsScreen::onKey(App& app, const KeyEvent& e) {
  GpsService& gps = *app.services().gps;
  if (e.is('a')) {
    if (!gps.state().valid) {
      app.notify("Pas de position", "Attendre le fix GPS (en extérieur)", 2500);
      return true;
    }
    bool ok = app.services().mesh->sendAdvert(true, true);
    app.notify(ok ? "Annonce envoyée" : "Annonce impossible", ok ? "Avec ta position GPS" : "Radio indisponible", 2000);
    return true;
  }
  if (e.is('b')) {
    app.push(&_compass);
    return true;
  }
  if (e.is('r')) {
    if (_confirm.check(app, 0, "Redémarrer le GPS à froid ?")) {
      gps.coldRestart();
      app.notify("GPS redémarré", "Le fix peut prendre quelques minutes", 2500);
    }
    return true;
  }
  return false;
}

bool GpsScreen::tick(App& app) {
  if (app.now() - _lastRefresh >= 1000) { _lastRefresh = app.now(); return true; }
  return false;
}

void GpsScreen::draw(App& app, lgfx::LovyanGFX& g) {
  GpsService& gps = *app.services().gps;
  const GpsState& s = gps.state();
  const int tz = app.services().settings->get().utcOffsetMinutes;
  char v[8][40];
  const char* labels[8] = {"État", "Latitude", "Longitude", "Altitude", "HDOP", "Vitesse", "Heure GPS",
                           "Dernier fix"};
  uint16_t stateColor = DIM;
  if (!gps.present()) {
    snprintf(v[0], 40, "aucun signal du module");
    stateColor = COLOR_BAD;
  } else if (s.valid) {
    snprintf(v[0], 40, "fix %s \xC2\xB7 %u sat.", s.fixType == 2 ? "2D" : "3D", s.sats);
    stateColor = COLOR_OK;
  } else {
    snprintf(v[0], 40, "recherche... %u sat.", s.sats);
    stateColor = COLOR_WARN;
  }
  bool pos = s.valid || s.fixMillis;
  if (pos) {
    formatCoord(s.lat, true, v[1], 40);
    formatCoord(s.lon, false, v[2], 40);
    snprintf(v[3], 40, "%.0f m", s.altM);
  } else {
    strcpy(v[1], "-");
    strcpy(v[2], "-");
    strcpy(v[3], "-");
  }
  const char* quality = s.hdop < 1.5f ? "excellente" : s.hdop < 3 ? "bonne" : s.hdop < 6 ? "moyenne" : "faible";
  if (s.sats) snprintf(v[4], 40, "%.1f (%s)", s.hdop, quality);
  else strcpy(v[4], "-");
  if (s.valid && s.speedKmh >= 1.0f) {
    snprintf(v[5], 40, "%.1f km/h  %s %.0f\xC2\xB0", s.speedKmh, geo::compassLabel(s.courseDeg), s.courseDeg);
  } else if (s.valid) {
    strcpy(v[5], "à l'arrêt");
  } else {
    strcpy(v[5], "-");
  }
  if (s.utcUnix) {
    uint32_t t = s.utcUnix + tz * 60;
    snprintf(v[6], 40, "%02u:%02u:%02u", (unsigned)(t / 3600 % 24), (unsigned)(t / 60 % 60), (unsigned)(t % 60));
  } else {
    strcpy(v[6], "-");
  }
  if (s.fixMillis) {
    char ago[12];
    formatAgo((app.now() - s.fixMillis) / 1000, ago, sizeof(ago));
    snprintf(v[7], 40, "il y a %s", ago);
  } else if (gps.present()) {
    snprintf(v[7], 40, "jamais (%u trames)", (unsigned)s.sentences);
  } else {
    snprintf(v[7], 40, "module muet");
  }
  for (int i = 0; i < 8; i++) {
    drawListRow(g, CONTENT_Y + 1 + i * LINE_H, labels[i], v[i], false, i == 0 ? stateColor : FG, true);
  }
}

// =============================================================== CompassScreen

bool CompassScreen::pickTarget(App& app, int step, ContactView& out) {
  MeshService& mesh = *app.services().mesh;
  int n = mesh.contactCount();
  int cur = -1, count = 0;
  ContactView c;
  // Contacts with a known position only
  for (int i = 0; i < n; i++) {
    if (!mesh.contactAt(i, c) || !c.hasPos) continue;
    if (c.key == _target) cur = count;
    count++;
  }
  if (!count) return false;
  int want = cur < 0 ? 0 : ((cur + step) % count + count) % count;
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (!mesh.contactAt(i, c) || !c.hasPos) continue;
    if (k++ == want) {
      out = c;
      _target = c.key;
      return true;
    }
  }
  return false;
}

bool CompassScreen::onKey(App& app, const KeyEvent& e) {
  ContactView c;
  if (e.code == KeyCode::Left) { pickTarget(app, -1, c); return true; }
  if (e.code == KeyCode::Right) { pickTarget(app, +1, c); return true; }
  if (e.is('m') && _chat && pickTarget(app, 0, c)) {
    _chat->open(app, c.key, c.name);
    app.push(_chat);
    return true;
  }
  return false;
}

bool CompassScreen::tick(App& app) {
  if (app.now() - _lastRefresh >= 500) { _lastRefresh = app.now(); return true; }
  return false;
}

void CompassScreen::draw(App& app, lgfx::LovyanGFX& g) {
  ContactView c;
  if (!pickTarget(app, 0, c)) {
    g.setTextDatum(lgfx::middle_center);
    g.setFont(&fontText);
    g.setTextColor(DIM);
    g.drawString("Aucun contact avec position", SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2 - 8);
    g.drawString("(il faut qu'il partage sa position)", SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2 + 8);
    g.setTextDatum(lgfx::top_left);
    return;
  }
  const GpsState& s = app.services().gps->state();
  DeviceStatus st = app.services().sys->status();
  const bool me = st.hasPosition;
  const double tlat = c.latE6 / 1e6, tlon = c.lonE6 / 1e6;

  // ---- dial
  const int cx = 58, cy = CONTENT_Y + CONTENT_H / 2, r = 50;
  g.drawCircle(cx, cy, r, DIM);
  g.drawCircle(cx, cy, r - 1, BAR_BG);
  const bool moving = s.valid && s.speedKmh >= 3.0f;
  const double ref = moving ? s.courseDeg : 0;   // what "up" means
  // tick marks every 45 degrees, north marker
  for (int k = 0; k < 8; k++) {
    double a = (k * 45 - ref) * geo::DEG;
    int x1 = cx + (int)lround((r - 4) * sin(a)), y1 = cy - (int)lround((r - 4) * cos(a));
    int x2 = cx + (int)lround(r * sin(a)), y2 = cy - (int)lround(r * cos(a));
    g.drawLine(x1, y1, x2, y2, k == 0 ? COLOR_BAD : DIM);
  }
  {
    double a = -ref * geo::DEG;
    g.setFont(&fontSmall);
    g.setTextColor(COLOR_BAD);
    g.setTextDatum(lgfx::middle_center);
    g.drawString("N", cx + (int)lround((r - 11) * sin(a)), cy - (int)lround((r - 11) * cos(a)));
  }

  double dist = 0, brg = 0;
  if (me) {
    dist = geo::distanceM(st.lat, st.lon, tlat, tlon);
    brg = geo::bearingDeg(st.lat, st.lon, tlat, tlon);
    double a = (brg - ref) * geo::DEG;
    auto pt = [&](double ang, double rad, int& x, int& y) {
      x = cx + (int)lround(rad * sin(ang));
      y = cy - (int)lround(rad * cos(ang));
    };
    int tx, ty, lx, ly, rx, ry, bx, by;
    pt(a, r - 9, tx, ty);
    pt(a + 2.5, r * 0.55, lx, ly);
    pt(a - 2.5, r * 0.55, rx, ry);
    pt(a + 3.14159, r * 0.25, bx, by);
    g.fillTriangle(tx, ty, lx, ly, bx, by, ACCENT);
    g.fillTriangle(tx, ty, rx, ry, bx, by, 0x03EF);
    g.fillCircle(cx, cy, 3, FG);
  } else {
    g.setFont(&fontSmall);
    g.setTextColor(COLOR_WARN);
    g.setTextDatum(lgfx::middle_center);
    g.drawString("GPS ?", cx, cy);
  }
  g.setTextDatum(lgfx::top_left);

  // ---- info panel
  const int x = 118, w = SCREEN_W - x - 4;
  char buf[48], line[48];
  int y = CONTENT_Y + 2;
  g.setFont(&fontBold);
  g.setTextColor(FG);
  fitText(g, c.name, w, line, sizeof(line));
  g.drawString(line, x, y);
  y += LINE_H + 4;
  if (me) {
    formatDistance(dist, buf, sizeof(buf));
    g.setFont(&fontBold);
    g.setTextSize(2);
    g.setTextColor(ACCENT);
    fitText(g, buf, w, line, sizeof(line));
    g.drawString(line, x, y);
    g.setTextSize(1);
    y += 2 * LINE_H + 2;
    g.setFont(&fontText);
    g.setTextColor(FG);
    snprintf(buf, sizeof(buf), "%s  %.0f\xC2\xB0", geo::compassLabel(brg), brg);
    g.drawString(buf, x, y);
  } else {
    g.setFont(&fontText);
    g.setTextColor(COLOR_WARN);
    g.drawString("En attente", x, y);
    g.drawString("du fix GPS", x, y + LINE_H);
    y += LINE_H;
  }
  y += LINE_H + 2;
  g.setFont(&fontSmall);
  g.setTextColor(DIM);
  uint32_t now = st.unixTime;
  if (c.lastHeard && clockValid(now) && now >= c.lastHeard) {
    char ago[12];
    formatAgo(now - c.lastHeard, ago, sizeof(ago));
    snprintf(buf, sizeof(buf), "vu il y a %s", ago);
  } else {
    snprintf(buf, sizeof(buf), "position ancienne");
  }
  fitText(g, buf, w, line, sizeof(line));
  g.drawString(line, x, y);
  y += 11;
  g.drawString(moving ? "haut = ma direction" : "haut = nord", x, y);
}
