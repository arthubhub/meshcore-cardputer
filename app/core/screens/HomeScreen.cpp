#include "HomeScreen.h"
#include "../RadioPresets.h"
#include "../drivers/Mentions.h"
#include "../drivers/RadioConfig.h"
#include "../drivers/TrafficTracer.h"
#include "../Widgets.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

void PlaceholderScreen::draw(App&, lgfx::LovyanGFX& g) {
  g.setFont(&fontBold);
  g.setTextColor(FG);
  g.setTextDatum(lgfx::middle_center);
  g.drawString(_title, SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2 - 8);
  g.setFont(&fontText);
  g.setTextColor(DIM);
  g.drawString(_phase, SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2 + 8);
  g.setTextDatum(lgfx::top_left);
}

// ---------------------------------------------------------------- what each section says about itself

namespace {

void messagesSummary(App& app, char l[3][48]) {
  MessageStore& st = app.services().mesh->messages();
  const int unread = st.unreadTotal();
  if (unread) snprintf(l[0], 48, tr("%d unread", "%d non lu%s"), unread, unread > 1 ? "s" : "");
  else snprintf(l[0], 48, "%s", tr("Nothing new", "Rien de nouveau"));
  snprintf(l[1], 48, tr("%d conversations", "%d conversations"), st.count());
  if (const ConvMeta* c = st.count() ? st.at(0) : nullptr) {
    char preview[MSG_TEXT_MAX + 1];
    MentionText::toDisplay(c->last, preview, sizeof(preview), nullptr, 0);
    snprintf(l[2], 48, "%s: %s", c->name, preview);
  }
}

void contactsSummary(App& app, char l[3][48]) {
  MeshService& m = *app.services().mesh;
  snprintf(l[0], 48, tr("%d contacts of %d", "%d contacts sur %d"), m.contactCount(), m.contactCapacity());
  NeighbourInfo nb[8];
  const int n = m.neighbours(nb, 8);
  snprintf(l[1], 48, tr("%d neighbours heard", "%d voisins entendus"), n);
}

void gpsSummary(App& app, char l[3][48]) {
  GpsService* gps = app.services().gps;
  if (!gps || !gps->present()) {
    snprintf(l[0], 48, "%s", tr("No GPS answer", "Le GPS ne répond pas"));
    return;
  }
  const GpsState& s = gps->state();
  if (s.valid) {
    snprintf(l[0], 48, tr("Fix, %u satellites", "Position, %u satellites"), s.sats);
    snprintf(l[1], 48, "%.5f, %.5f", s.lat, s.lon);
    snprintf(l[2], 48, "%s", tr("Compass to a contact", "Boussole vers un contact"));
  } else {
    snprintf(l[0], 48, tr("Searching, %u satellites", "Recherche, %u satellites"), s.sats);
  }
}

void mapSummary(App& app, char l[3][48]) {
  TileSource* t = app.services().tiles;
  int zmin = 0, zmax = 0;
  if (t && t->available() && t->zoomRange(zmin, zmax)) {
    snprintf(l[0], 48, tr("Offline map, zoom %d-%d", "Carte hors ligne, zoom %d-%d"), zmin, zmax);
    snprintf(l[1], 48, "%s", tr("Contacts and routes", "Contacts et trajets"));
  } else {
    snprintf(l[0], 48, "%s", tr("No map on the SD card", "Pas de carte sur la SD"));
  }
}

void nearbySummary(App& app, char l[3][48]) {
  const int n = app.services().mesh->discovery().count();
  if (n) snprintf(l[0], 48, tr("%d repeaters in range", "%d routeurs à portée"), n);
  else snprintf(l[0], 48, "%s", tr("Search the repeaters", "Chercher les routeurs"));
  snprintf(l[1], 48, "%s", tr("in direct range", "à portée directe"));
}

void packetsSummary(App& app, char l[3][48]) {
  MeshService& m = *app.services().mesh;
  snprintf(l[0], 48, tr("%d packets in the log", "%d paquets au journal"), m.packetLog().count());
  RadioStats r = m.radioStats();
  snprintf(l[1], 48, tr("%lu received, %lu sent", "%lu reçus, %lu envoyés"), (unsigned long)r.rxPackets,
           (unsigned long)r.txPackets);
}

void tracerSummary(App& app, char l[3][48]) {
  TrafficTracer& t = app.services().mesh->tracer();
  int routers = 0, busiest = -1;
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
    if (!t.node(i).used || t.node(i).kind != TrafficTracer::Kind::Router) continue;
    routers++;
    if (busiest < 0 || t.usage(i) > t.usage(busiest)) busiest = i;
  }
  if (t.me() < 0) {
    snprintf(l[0], 48, "%s", tr("Routes of the packets heard", "Trajets des paquets entendus"));
    snprintf(l[1], 48, "%s", tr("Nothing yet", "Rien pour l'instant"));
    return;
  }
  snprintf(l[0], 48, tr("%d repeaters used, 20 min", "%d routeurs utilisés, 20 min"), routers);
  snprintf(l[1], 48, tr("%d packets traced", "%d paquets tracés"), t.packets());
  if (busiest >= 0 && t.node(busiest).name[0]) snprintf(l[2], 48, tr("Busiest: %s", "Le plus chargé : %s"), t.node(busiest).name);
}

void settingsSummary(App& app, char l[3][48]) {
  const UiSettings& s = app.services().settings->get();
  snprintf(l[0], 48, "%s", s.nodeName);
  snprintf(l[1], 48, tr("Radio: %s", "Radio : %s"), RadioConfig::name(s));
  snprintf(l[2], 48, tr("Theme: %s", "Thème : %s"), app.themes().find(s.theme).name);
}

}  // namespace

NavSection::Summary HomeScreen::defaultSummary(NavIcon icon) {
  switch (icon) {
    case NavIcon::MESSAGES: return messagesSummary;
    case NavIcon::CONTACTS: return contactsSummary;
    case NavIcon::GPS: return gpsSummary;
    case NavIcon::MAP: return mapSummary;
    case NavIcon::NEARBY: return nearbySummary;
    case NavIcon::PACKETS: return packetsSummary;
    case NavIcon::SETTINGS: return settingsSummary;
    case NavIcon::TRACER: return tracerSummary;
    default: return nullptr;
  }
}

// ---------------------------------------------------------------- HomeScreen

HomeScreen::HomeScreen(Screen* messages, Screen* contacts, Screen* gps, Screen* map, Screen* nearby,
                       Screen* settings) {
  addSection(NavSection("Messages", "Messages", 'm', NavIcon::MESSAGES, messages, messagesSummary));
  addSection(NavSection("Contacts", "Contacts", 'c', NavIcon::CONTACTS, contacts, contactsSummary));
  addSection(NavSection("GPS & compass", "GPS et boussole", 'g', NavIcon::GPS, gps, gpsSummary));
  addSection(NavSection("Map", "Carte", 'k', NavIcon::MAP, map, mapSummary, "This build has no map (see the _map build)",
                        "Firmware sans carte (version _map en option)"));
  addSection(NavSection("Nearby repeaters", "Routeurs proches", 'n', NavIcon::NEARBY, nearby, nearbySummary));
  addSection(NavSection("Settings", "Réglages", 's', NavIcon::SETTINGS, settings, settingsSummary));
}

void HomeScreen::addSection(const NavSection& s) {
  if (_n >= MAX_SECTIONS) return;
  // Settings stays the last page
  int at = _n;
  if (_n && _s[_n - 1].icon == NavIcon::SETTINGS && s.icon != NavIcon::SETTINGS) {
    at = _n - 1;
    _s[_n] = _s[_n - 1];
  }
  _s[at] = s;
  _n++;
  _carousel.setCount(_n + 1);
}

void HomeScreen::open(App& app, int i) {
  if (i < 0 || i >= _n) return;
  const NavSection& s = _s[i];
  if (s.screen) app.push(s.screen);
  else app.notify(tr("Not included", "Non inclus"), tr(s.missingEn ? s.missingEn : "", s.missingFr ? s.missingFr : ""), 2500);
}

const char* HomeScreen::help() const {
  // The letters of the sections (M, C, G...) are on their pages: the help keeps to the gestures
  return tr(",/ slide   Enter open   . list", ",/ défiler   Entrée ouvrir   . liste");
}

bool HomeScreen::onKey(App& app, const KeyEvent& e) {
  for (int i = 0; i < _n; i++) {                // the letters still open their sections
    if (_s[i].key && e.is(_s[i].key)) {
      open(app, i);
      return true;
    }
  }
  switch (e.code) {
    case KeyCode::Left: _carousel.slide(-1, app.now()); return true;
    case KeyCode::Right: _carousel.slide(+1, app.now()); return true;
    case KeyCode::Down:
      app.push(&_menu);
      return true;
    case KeyCode::Enter:
      if (_carousel.page() == 0) app.push(&_menu);   // the dashboard: the list of everything
      else open(app, _carousel.page() - 1);
      return true;
    default:
      return false;
  }
}

bool HomeScreen::tick(App& app) {
  if (_carousel.sliding(app.now()) || _carousel.leaving(app.now() - 20) >= 0) return true;   // one more frame at the end
  int unread = app.services().mesh->unreadCount();
  bool changed = unread != _lastUnread;
  _lastUnread = unread;
  if (app.now() - _lastRefresh >= 5000) {        // neighbour ages, GPS, counts
    _lastRefresh = app.now();
    changed = true;
  }
  return changed;
}

static void sectionTitle(lgfx::LovyanGFX& g, const char* text, int x, int y) {
  g.setFont(&fontBold);
  g.setTextColor(ACCENT);
  g.drawString(text, x + MARGIN, y);
  int w = g.textWidth(text);
  g.drawFastHLine(x + MARGIN + w + 4, y + LINE_H / 2, SCREEN_W - w - 2 * MARGIN - 4, DIM);
}

void HomeScreen::drawDashboard(App& app, lgfx::LovyanGFX& g, int ox) {
  MeshService& mesh = *app.services().mesh;
  const int utcOffset = app.services().settings->get().utcOffsetMinutes;
  char line[128], fitted[128];
  int y = CONTENT_Y + 1;

  // Identity and radio settings
  RadioStats rs = mesh.radioStats();
  g.setFont(&fontBold);
  g.setTextColor(FG);
  g.drawString(mesh.selfName(), ox + MARGIN, y);
  g.setFont(&fontSmall);
  g.setTextColor(DIM);
  g.setTextDatum(lgfx::top_right);
  snprintf(line, sizeof(line), "%.3f MHz SF%u BW%g", rs.freqMHz, rs.spreadingFactor, rs.bandwidthKHz);
  g.drawString(line, ox + SCREEN_W - MARGIN, y + 2);
  g.setTextDatum(lgfx::top_left);
  y += LINE_H + 1;

  // Latest conversations (from the in-RAM index: no flash access)
  sectionTitle(g, "Messages", ox, y);
  y += LINE_H;
  MessageStore& store = mesh.messages();
  uint32_t now = app.services().sys->status().unixTime;
  g.setFont(&fontText);
  int shown = 0;
  for (int i = 0; i < store.count() && shown < 3; i++) {
    const ConvMeta* c = store.at(i);
    if (!c || !c->total) continue;
    formatWhen(c->lastTs, now, utcOffset, line, sizeof(line));
    g.setTextColor(DIM);
    g.drawString(line, ox + MARGIN, y);
    int x = MARGIN + g.textWidth("00:00 ");
    char preview[MSG_TEXT_MAX + 1];
    MentionText::toDisplay(c->last, preview, sizeof(preview), nullptr, 0);   // "@[name]" -> "@name"
    snprintf(line, sizeof(line), "%s: %s%s", c->name, c->lastOutgoing ? tr("me > ", "moi > ") : "", preview);
    fitText(g, line, SCREEN_W - x - MARGIN - 6, fitted, sizeof(fitted));
    g.setTextColor(c->unread ? FG : DIM);
    g.drawString(fitted, ox + x, y);
    if (c->unread) g.fillCircle(ox + SCREEN_W - MARGIN - 2, y + 6, 2, COLOR_WARN);
    y += LINE_H;
    shown++;
  }
  if (shown == 0) {
    g.setTextColor(DIM);
    g.drawString(tr("No messages", "Aucun message"), ox + MARGIN, y);
    y += LINE_H;
  }

  // Neighbours heard recently
  sectionTitle(g, tr("Neighbours", "Voisins"), ox, y);
  y += LINE_H;
  NeighbourInfo nb[2];
  const int room = (CONTENT_Y + CONTENT_H - 6 - y) / LINE_H;   // above the page dots
  int k = mesh.neighbours(nb, room < 2 ? (room < 1 ? 0 : 1) : 2);
  g.setFont(&fontText);
  if (k == 0 && room > 0) {
    g.setTextColor(DIM);
    g.drawString(tr("Nobody heard yet", "Personne entendu"), ox + MARGIN, y);
  }
  for (int i = 0; i < k; i++) {
    uint32_t a = nb[i].lastHeardSecs;
    char age[12];
    if (a < 60) snprintf(age, sizeof(age), "%us", (unsigned)a);
    else if (a < 3600) snprintf(age, sizeof(age), "%um", (unsigned)(a / 60));
    else snprintf(age, sizeof(age), "%uh", (unsigned)(a / 3600));
    char right[40];
    snprintf(right, sizeof(right), "SNR %.1f  %s", nb[i].snr, age);
    g.setTextColor(DIM);
    g.setTextDatum(lgfx::top_right);
    g.drawString(right, ox + SCREEN_W - MARGIN, y);
    g.setTextDatum(lgfx::top_left);
    g.setTextColor(FG);
    fitText(g, nb[i].name, SCREEN_W - 2 * MARGIN - g.textWidth(right) - 6, fitted, sizeof(fitted));
    g.drawString(fitted, ox + MARGIN, y);
    y += LINE_H;
  }
}

void HomeScreen::drawCard(App& app, lgfx::LovyanGFX& g, const NavSection& s, int ox) {
  const int top = CONTENT_Y + 10;
  drawNavIcon(g, s.icon, ox + 14, top + 4, 2, s.screen ? ACCENT : DIM);
  const int tx = ox + 60;
  g.setFont(&fontBold);
  g.setTextColor(FG);
  g.drawString(s.title(), tx, top + 2);
  if (s.key) {                                   // its letter, for the keyboard
    char k[4] = {(char)(s.key - 'a' + 'A'), 0};
    g.setFont(&fontSmall);
    g.setTextColor(DIM);
    g.setTextDatum(lgfx::top_right);
    g.drawRoundRect(ox + SCREEN_W - 18, top + 1, 13, 12, 2, DIM);
    g.setTextDatum(lgfx::top_center);
    g.drawString(k, ox + SCREEN_W - 12, top + 2);
    g.setTextDatum(lgfx::top_left);
  }
  char lines[3][48] = {};
  if (!s.screen) snprintf(lines[0], 48, "%s", tr(s.missingEn ? s.missingEn : "", s.missingFr ? s.missingFr : ""));
  else if (s.summary) s.summary(app, lines);
  g.setFont(&fontText);
  char fitted[64];
  for (int i = 0; i < 3; i++) {
    if (!lines[i][0]) continue;
    g.setTextColor(i == 0 ? FG : DIM);
    fitText(g, lines[i], SCREEN_W - 66, fitted, sizeof(fitted));
    g.drawString(fitted, tx, top + 20 + i * LINE_H);
  }
  g.setFont(&fontSmall);
  g.setTextColor(s.screen ? ACCENT : DIM);
  g.drawString(s.screen ? tr("Enter: open", "Entrée : ouvrir") : tr("Not in this build", "Absent de ce firmware"), tx,
               top + 20 + 3 * LINE_H + 4);
}

void HomeScreen::drawPage(App& app, lgfx::LovyanGFX& g, int page, int ox) {
  if (page == 0) drawDashboard(app, g, ox);
  else drawCard(app, g, _s[page - 1], ox);
}

void HomeScreen::drawDots(lgfx::LovyanGFX& g) {
  // One dot per page; the current one wider
  const int n = _carousel.count(), gap = 7;
  const int w = (n - 1) * gap + 6;
  int x = (SCREEN_W - w) / 2;
  const int y = CONTENT_Y + CONTENT_H - 4;
  for (int i = 0; i < n; i++) {
    if (i == _carousel.page()) {
      g.fillRoundRect(x - 1, y - 1, 7, 3, 1, ACCENT);
      x += gap + 3;
    } else {
      g.fillRect(x, y, 2, 2, DIM);
      x += gap - 1;
    }
  }
}

void HomeScreen::draw(App& app, lgfx::LovyanGFX& g) {
  const uint32_t now = app.now();
  g.setClipRect(0, CONTENT_Y, SCREEN_W, CONTENT_H);
  const int shift = _carousel.shift(now, SCREEN_W);
  const int leaving = _carousel.leaving(now);
  if (leaving >= 0) {
    // The arriving page comes from the side `shift` points to; the leaving one goes the other way
    drawPage(app, g, leaving, shift > 0 ? shift - SCREEN_W : shift + SCREEN_W);
  }
  drawPage(app, g, _carousel.page(), shift);
  g.clearClipRect();
  drawDots(g);
}

// ---------------------------------------------------------------- MenuScreen

void MenuScreen::onEnter(App&) {
  if (_sel >= _home.sectionCount()) _sel = 0;
}

bool MenuScreen::onKey(App& app, const KeyEvent& e) {
  const int n = _home.sectionCount();
  const int visible = CONTENT_H / ROW_H;
  switch (e.code) {
    case KeyCode::Up: _sel = (_sel - 1 + n) % n; break;
    case KeyCode::Down: _sel = (_sel + 1) % n; break;
    case KeyCode::Enter:
      app.pop();
      _home.open(app, _sel);
      return true;
    default:
      for (int i = 0; i < n; i++) {                // the letters work here too
        if (_home.section(i).key && e.is(_home.section(i).key)) {
          app.pop();
          _home.open(app, i);
          return true;
        }
      }
      return false;
  }
  if (_sel < _top) _top = _sel;
  if (_sel >= _top + visible) _top = _sel - visible + 1;
  return true;
}

void MenuScreen::draw(App& app, lgfx::LovyanGFX& g) {
  const int n = _home.sectionCount();
  const int visible = CONTENT_H / ROW_H;
  char fitted[48];
  for (int r = 0; r < visible && _top + r < n; r++) {
    const int i = _top + r;
    const NavSection& s = _home.section(i);
    const int y = CONTENT_Y + 1 + r * ROW_H;
    const bool sel = i == _sel;
    if (sel) g.fillRect(0, y, SCREEN_W - 4, ROW_H, SELECT_BG);
    drawNavIcon(g, s.icon, 4, y + 1, 1, s.screen ? ACCENT : DIM);
    g.setFont(&fontText);
    g.setTextColor(s.screen ? FG : DIM);
    g.drawString(s.title(), 26, y + 3);
    const int titleW = g.textWidth(s.title());
    char lines[3][48] = {};
    if (s.screen && s.summary) s.summary(app, lines);
    g.setFont(&fontSmall);
    g.setTextColor(DIM);
    g.setTextDatum(lgfx::top_right);
    fitText(g, lines[0], SCREEN_W - 26 - titleW - 16, fitted, sizeof(fitted));
    g.drawString(fitted, SCREEN_W - 8, y + 5);
    g.setTextDatum(lgfx::top_left);
  }
  drawScrollbar(g, _top, visible, n);
}
