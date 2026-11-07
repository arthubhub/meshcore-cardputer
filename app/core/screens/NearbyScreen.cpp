#include "NearbyScreen.h"
#include "MessagingScreens.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

static ConvKey keyOf(const DiscoveredNode& n) {
  ConvKey k;
  k.kind = ConvKey::DIRECT;
  memcpy(k.id, n.prefix, sizeof(k.id));
  return k;
}

void NearbyScreen::search(App& app) {
  MeshService& mesh = *app.services().mesh;
  const NodeDiscovery& d = mesh.discovery();
  uint32_t wait = d.secondsUntilNext(app.now());
  if (wait) {
    char text[48];
    snprintf(text, sizeof(text), tr("Next search in %u s", "Prochaine recherche dans %u s"), (unsigned)wait);
    app.notify(tr("Please wait", "Patiente"), text, 2000);
  } else if (!mesh.startDiscovery()) {
    app.notify(tr("Cannot search", "Recherche impossible"), tr("Radio unavailable", "Radio indisponible"), 2000);
  }
}

void NearbyScreen::onEnter(App& app) {
  _lastCount = -1;
  if (app.services().mesh->discovery().canStart(app.now())) search(app);
}

void NearbyScreen::remember(App& app) {
  const NodeDiscovery& d = app.services().mesh->discovery();
  if (_list.sel >= 0 && _list.sel < d.count()) memcpy(_sel, d.at(_list.sel).prefix, sizeof(_sel));
}

void NearbyScreen::syncSelection(App& app) {
  const NodeDiscovery& d = app.services().mesh->discovery();
  for (int i = 0; i < d.count(); i++) {
    if (memcmp(d.at(i).prefix, _sel, sizeof(_sel)) == 0) { _list.sel = i; break; }
  }
  _list.clamp(d.count(), ROWS);
  remember(app);
}

bool NearbyScreen::onKey(App& app, const KeyEvent& e) {
  MeshService& mesh = *app.services().mesh;
  const NodeDiscovery& d = mesh.discovery();
  int n = d.count();
  syncSelection(app);
  if (e.code == KeyCode::Up) { _list.move(-1, n, ROWS); remember(app); return true; }
  if (e.code == KeyCode::Down) { _list.move(1, n, ROWS); remember(app); return true; }
  if (e.is('r')) { search(app); return true; }
  if (e.code == KeyCode::Enter && n > 0) {
    ContactView c;
    ConvKey k = keyOf(d.at(_list.sel));
    if (mesh.findContact(k, c)) {
      _detail.show(c.key);
      app.push(&_detail);
    } else {
      app.notify(tr("Not in your contacts yet", "Pas encore dans tes contacts"),
                 tr("Its name comes with its next advert", "Son nom arrivera avec sa prochaine annonce"), 3000);
    }
    return true;
  }
  return false;
}

bool NearbyScreen::tick(App& app) {
  const NodeDiscovery& d = app.services().mesh->discovery();
  uint32_t now = app.now();
  uint32_t secs = d.searching(now) ? d.secondsLeft(now) : d.secondsUntilNext(now);
  bool changed = d.count() != _lastCount || secs != _lastSecs;
  _lastCount = d.count();
  _lastSecs = secs;
  return changed;
}

void NearbyScreen::draw(App& app, lgfx::LovyanGFX& g) {
  MeshService& mesh = *app.services().mesh;
  const NodeDiscovery& d = mesh.discovery();
  uint32_t now = app.now();
  int n = d.count();
  snprintf(_title, sizeof(_title), tr("Nearby routers (%d)", "Routeurs proches (%d)"), n);

  char status[48];
  if (d.searching(now)) snprintf(status, sizeof(status), tr("Searching... %u s", "Recherche... %u s"), (unsigned)d.secondsLeft(now));
  else if (d.secondsUntilNext(now)) snprintf(status, sizeof(status), tr("Next search in %u s", "Prochaine dans %u s"), (unsigned)d.secondsUntilNext(now));
  else snprintf(status, sizeof(status), "%s", tr("R: search again", "R : chercher encore"));
  drawListRow(g, CONTENT_Y + 1, status, n ? tr("SNR out/in", "SNR aller/retour") : "", false, DIM, true);

  if (n == 0) {
    g.setFont(&fontText);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(d.searching(now) ? FG : DIM);
    g.drawString(d.searching(now) ? tr("Waiting for answers", "En attente de réponses") : tr("No router answered", "Aucun routeur n'a répondu"),
                 SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2);
    g.setTextColor(DIM);
    g.drawString(tr("Direct radio range only", "Portée radio directe uniquement"), SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2 + LINE_H + 2);
    g.setTextDatum(lgfx::top_left);
    return;
  }

  syncSelection(app);
  for (int i = 0; i < ROWS && _list.top + i < n; i++) {
    int k = _list.top + i;
    const DiscoveredNode& node = d.at(k);
    char name[40], right[24];
    ContactView c;
    if (mesh.findContact(keyOf(node), c)) snprintf(name, sizeof(name), "%s", c.name);
    else snprintf(name, sizeof(name), "%02x%02x%02x%02x", node.prefix[0], node.prefix[1], node.prefix[2], node.prefix[3]);
    snprintf(right, sizeof(right), "%+.1f / %+.1f", node.snrThere, node.snrHere);
    drawListRow(g, CONTENT_Y + 1 + (i + 1) * LINE_H, name, right, k == _list.sel, DIM, false);
  }
  drawScrollbar(g, _list.top, ROWS, n);
}
