#include "HomeScreen.h"
#include "../Widgets.h"
#include <stdio.h>

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

bool HomeScreen::onKey(App& app, const KeyEvent& e) {
  if (e.is('m')) { app.push(_messages); return true; }
  if (e.is('c')) { app.push(_contacts); return true; }
  if (e.is('g')) { app.push(_gps); return true; }
  if (e.is('k')) {
    if (_map) app.push(_map);
    else app.notify("Carte non incluse", "Firmware sans carte (version _map en option)", 2500);
    return true;
  }
  if (e.is('s')) { app.push(_settings); return true; }
  return false;
}

bool HomeScreen::tick(App& app) {
  int unread = app.services().mesh->unreadCount();
  bool changed = unread != _lastUnread;
  _lastUnread = unread;
  if (app.now() - _lastRefresh >= 5000) {   // neighbour ages ("12 s")
    _lastRefresh = app.now();
    changed = true;
  }
  return changed;
}

static void sectionTitle(lgfx::LovyanGFX& g, const char* text, int y) {
  g.setFont(&fontBold);
  g.setTextColor(ACCENT);
  g.drawString(text, MARGIN, y);
  int w = g.textWidth(text);
  g.drawFastHLine(MARGIN + w + 4, y + LINE_H / 2, SCREEN_W - w - 2 * MARGIN - 4, DIM);
}

void HomeScreen::draw(App& app, lgfx::LovyanGFX& g) {
  MeshService& mesh = *app.services().mesh;
  const int utcOffset = app.services().settings->get().utcOffsetMinutes;
  char line[128], fitted[128];
  int y = CONTENT_Y + 1;

  // Identity and radio settings
  RadioStats rs = mesh.radioStats();
  g.setFont(&fontBold);
  g.setTextColor(FG);
  g.drawString(mesh.selfName(), MARGIN, y);
  g.setFont(&fontSmall);
  g.setTextColor(DIM);
  g.setTextDatum(lgfx::top_right);
  snprintf(line, sizeof(line), "%.3f MHz SF%u BW%.1f", rs.freqMHz, rs.spreadingFactor, rs.bandwidthKHz);
  g.drawString(line, SCREEN_W - MARGIN, y + 2);
  g.setTextDatum(lgfx::top_left);
  y += LINE_H + 1;

  // Latest conversations (from the in-RAM index: no flash access)
  sectionTitle(g, "Messages", y);
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
    g.drawString(line, MARGIN, y);
    int x = MARGIN + g.textWidth("00:00 ");
    snprintf(line, sizeof(line), "%s: %s%s", c->name, c->lastOutgoing ? "moi > " : "", c->last);
    fitText(g, line, SCREEN_W - x - MARGIN - 6, fitted, sizeof(fitted));
    g.setTextColor(c->unread ? FG : DIM);
    g.drawString(fitted, x, y);
    if (c->unread) g.fillCircle(SCREEN_W - MARGIN - 2, y + 6, 2, COLOR_WARN);
    y += LINE_H;
    shown++;
  }
  if (shown == 0) {
    g.setTextColor(DIM);
    g.drawString("Aucun message", MARGIN, y);
    y += LINE_H;
  }

  // Neighbours heard recently
  sectionTitle(g, "Voisins", y);
  y += LINE_H;
  NeighbourInfo nb[2];
  int k = mesh.neighbours(nb, 2);
  g.setFont(&fontText);
  if (k == 0) {
    g.setTextColor(DIM);
    g.drawString("Personne entendu", MARGIN, y);
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
    g.drawString(right, SCREEN_W - MARGIN, y);
    g.setTextDatum(lgfx::top_left);
    g.setTextColor(FG);
    fitText(g, nb[i].name, SCREEN_W - 2 * MARGIN - g.textWidth(right) - 6, fitted, sizeof(fitted));
    g.drawString(fitted, MARGIN, y);
    y += LINE_H;
  }
}
