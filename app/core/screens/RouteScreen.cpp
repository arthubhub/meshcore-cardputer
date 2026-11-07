#include "RouteScreen.h"
#include "MapScreen.h"
#include "MessagingScreens.h"
#include <stdio.h>

using namespace theme;

void RouteScreen::show(App& app, const MsgRoute& route, const RouteEndpoint& start, const RouteEndpoint& end) {
  RouteResolver(*app.services().mesh).resolve(route, start, end, _route);
  _layer.show(&_route);
  const char* name = _route.kind == PATH_PLANNED ? tr("Planned", "Prévue") : "Route";
  if (_route.hops) snprintf(_title, sizeof(_title), tr("%s, %d/%d placed", "%s, %d/%d situés"), name, _route.placed, _route.hops);
  else snprintf(_title, sizeof(_title), "%s", name);
}

const RoutePoint* RouteScreen::pointAt(int row, int* number) const {
  const int last = _route.count - 1;
  if (_route.notHeld) {
    if (row == last) return nullptr;
    if (row > last) row = last;
  }
  *number = row;
  return &_route.points[row];
}

void RouteScreen::openContact(App& app) {
  int number = 0;
  const RoutePoint* p = pointAt(_list.sel, &number);
  ContactView c;
  if (p && p->key.valid() && app.services().mesh->findContact(p->key, c)) {
    _contact.show(c.key);
    app.push(&_contact);
  } else if (p && p->state == HopState::Unknown) {
    app.notify(tr("Unknown repeater", "Routeur inconnu"), tr("No contact has this ID", "Aucun contact n'a cet ID"), 2500);
  }
}

void RouteScreen::openMap(App& app) {
  geo::Bounds b;
  if (!_map) {
    app.notify(tr("Map not included", "Carte non incluse"), tr("This build has no map", "Firmware sans carte"), 2500);
  } else if (!_layer.bounds(b)) {
    app.notify(tr("Nothing to show", "Rien à afficher"), tr("No position known on this route", "Aucune position connue"), 2500);
  } else {
    _map->showOverlay(&_layer);
    app.push(_map);
  }
}

bool RouteScreen::onKey(App& app, const KeyEvent& e) {
  if (e.code == KeyCode::Up) { _list.move(-1, rowCount(), LIST_VISIBLE_ROWS); return true; }
  if (e.code == KeyCode::Down) { _list.move(1, rowCount(), LIST_VISIBLE_ROWS); return true; }
  if (e.code == KeyCode::Enter) { openContact(app); return true; }
  if (e.is('t')) { openMap(app); return true; }
  return false;
}

static const char* stateText(const RoutePoint& p) {
  switch (p.state) {
    case HopState::Endpoint: return p.hasPos ? "" : tr("no position", "sans position");
    case HopState::Single: return p.hasPos ? "ok" : tr("no position", "sans position");
    case HopState::Ambiguous: return tr("uncertain", "incertain");
    default: return "?";
  }
}

void RouteScreen::draw(App&, lgfx::LovyanGFX& g) {
  const int rows = rowCount();
  _list.clamp(rows, LIST_VISIBLE_ROWS);
  for (int i = 0; i < LIST_VISIBLE_ROWS && _list.top + i < rows; i++) {
    const int k = _list.top + i;
    const int y = CONTENT_Y + 1 + i * LINE_H;
    int number = 0;
    const RoutePoint* p = pointAt(k, &number);
    char left[32], right[24];
    if (!p) {
      snprintf(left, sizeof(left), tr("+%d hops not kept", "+%d sauts non gardés"), _route.notHeld);
      drawListRow(g, y, left, "", k == _list.sel, DIM, true);
      continue;
    }
    if (p->state == HopState::Endpoint) {
      snprintf(left, sizeof(left), "%s", p->name);
    } else if (p->state == HopState::Unknown) {
      char id[8] = "";
      for (int b = 0, o = 0; b < p->hashBytes; b++) o += snprintf(id + o, sizeof(id) - o, "%02x", p->hash[b]);
      snprintf(left, sizeof(left), "%d %s", number, id);
    } else {
      snprintf(left, sizeof(left), "%d %s", number, p->name[0] ? p->name : "?");
    }
    if (p->state == HopState::Ambiguous) snprintf(right, sizeof(right), "%s (%u)", stateText(*p), p->candidates);
    else snprintf(right, sizeof(right), "%s", stateText(*p));
    const uint16_t color = p->state == HopState::Ambiguous ? COLOR_WARN : p->state == HopState::Unknown ? DIM : FG;
    drawListRow(g, y, left, right, k == _list.sel, color, p->state != HopState::Endpoint);
  }
  drawScrollbar(g, _list.top, LIST_VISIBLE_ROWS, rows);
}
