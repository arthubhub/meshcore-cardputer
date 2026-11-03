#include "MessagingScreens.h"
#include "GpsScreens.h"
#include "../Geo.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {

int utcOffset(App& app) { return app.services().settings->get().utcOffsetMinutes; }
uint32_t nowUnix(App& app) { return app.services().sys->status().unixTime; }

void hopsLabel(int8_t hops, char* out, size_t n) {
  if (hops < 0) snprintf(out, n, "chemin ?");
  else if (hops == 0) snprintf(out, n, "direct");
  else snprintf(out, n, "%d saut%s", hops, hops > 1 ? "s" : "");
}

const char* typePrefix(uint8_t type) {
  switch (type) {
    case CONTACT_REPEATER: return "[R] ";
    case CONTACT_ROOM: return "[S] ";
    case CONTACT_SENSOR: return "[C] ";
    default: return "";
  }
}

const char* typeName(uint8_t type) {
  switch (type) {
    case CONTACT_CHAT: return "Personne";
    case CONTACT_REPEATER: return "Répéteur";
    case CONTACT_ROOM: return "Salon";
    case CONTACT_SENSOR: return "Capteur";
    default: return "?";
  }
}

void emptyState(lgfx::LovyanGFX& g, const char* l1, const char* l2) {
  g.setTextDatum(lgfx::middle_center);
  g.setFont(&fontText);
  g.setTextColor(DIM);
  g.drawString(l1, SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2 - 8);
  g.drawString(l2, SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2 + 8);
  g.setTextDatum(lgfx::top_left);
}

}  // namespace

bool ConfirmPress::check(App& app, int t, const char* what) {
  if (target == t && (int32_t)(app.now() - until) < 0) {
    target = -1;
    return true;
  }
  target = t;
  until = app.now() + 3000;
  app.notify(what, "Appuyer encore sur D pour confirmer", 3000);
  return false;
}

// =============================================================== ChatScreen

void ChatScreen::open(App& app, const ConvKey& key, const char* name) {
  if (key != _key) _draft[0] = 0;   // the draft belongs to one conversation
  _key = key;
  utf8Copy(_title, sizeof(_title), name);
  _scroll = 0;
  _maxLen = app.services().mesh->maxTextLen(key);
  if (_maxLen > MSG_TEXT_MAX) _maxLen = MSG_TEXT_MAX;
}

void ChatScreen::onEnter(App& app) { reload(app); }

void ChatScreen::reload(App& app) {
  MessageStore& st = app.services().mesh->messages();
  if (const ConvMeta* c = st.find(_key)) utf8Copy(_title, sizeof(_title), c->name);
  _total = st.total(_key);
  if (_scroll > 0 && (uint32_t)_scroll >= _total) _scroll = _total ? _total - 1 : 0;
  _n = st.loadRecent(_key, _msgs, WINDOW, _scroll);
  _layoutValid = false;
  st.markRead(_key);
}

bool ChatScreen::onKey(App& app, const KeyEvent& e) {
  _cursorOn = true;
  _lastBlink = app.now();
  switch (e.code) {
    case KeyCode::Enter: {
      // trim trailing spaces
      size_t n = strlen(_draft);
      while (n && _draft[n - 1] == ' ') _draft[--n] = 0;
      if (!n) return true;
      if (app.services().mesh->sendText(_key, _draft)) {
        _draft[0] = 0;
        _scroll = 0;
      } else {
        app.notify("Envoi impossible", "Radio occupée ou contact inconnu", 2500);
      }
      reload(app);
      return true;
    }
    case KeyCode::Backspace:
      utf8PopBack(_draft);
      return true;
    case KeyCode::Up:
      if ((uint32_t)_scroll + 1 < _total) { _scroll++; reload(app); }
      return true;
    case KeyCode::Down:
      if (_scroll > 0) { _scroll--; reload(app); }
      return true;
    case KeyCode::Char: {
      size_t cap = _maxLen + 1;
      if (e.mods & MOD_ALT) composeAccent(_draft, cap, e.ch);
      else if (e.ch >= 0x20 && e.ch < 0x7F) {
        char s[2] = {e.ch, 0};
        utf8Append(_draft, cap, s);
      }
      return true;
    }
    default:
      return false;   // Esc: back
  }
}

bool ChatScreen::tick(App& app) {
  if (app.now() - _lastBlink >= 500) {
    _lastBlink = app.now();
    _cursorOn = !_cursorOn;
    return true;
  }
  return false;
}

bool ChatScreen::onEvent(App& app, const MeshEvent& ev) {
  if (ev.key != _key) return false;
  if (ev.type == MeshEvent::MESSAGE) {
    if (_scroll == 0) reload(app);   // keep the reading position when scrolled back
    else app.services().mesh->messages().markRead(_key);
    app.services().sys->playSound(SOUND_CHANNEL_MSG);   // discreet: the message is on screen
    return true;
  }
  if (ev.type == MeshEvent::DELIVERED || ev.type == MeshEvent::FAILED) {
    reload(app);
    if (ev.type == MeshEvent::FAILED) app.services().sys->playSound(SOUND_FAIL);
    return true;
  }
  return false;
}

static constexpr int COMPOSE_H = 16;
static constexpr int BUBBLE_TEXT_W = 176;
static constexpr int META_H = 10;

void ChatScreen::layout(lgfx::LovyanGFX& g) {
  for (int i = 0; i < _n; i++) {
    const StoredMsg& m = _msgs[i];
    Layout& l = _lay[i];
    g.setFont(&fontText);
    l.lines = wrapText(g, m.text, BUBBLE_TEXT_W, l.line, MAX_LINES);
    if (l.lines == 0) { l.lines = 1; l.line[0] = {0, 0}; }
    int w = 0;
    char buf[MSG_WRAP_BUF];
    for (int k = 0; k < l.lines; k++) {
      memcpy(buf, m.text + l.line[k].start, l.line[k].len);
      buf[l.line[k].len] = 0;
      int lw = g.textWidth(buf);
      if (lw > w) w = lw;
    }
    if (!m.outgoing && m.sender[0]) {
      g.setFont(&fontBold);
      int sw = g.textWidth(m.sender);
      if (sw > w) w = sw;
    }
    if (w < 60) w = 60;   // room for the time / status line
    l.width = w + 8;
    l.height = 3 + l.lines * LINE_H + META_H + ((!m.outgoing && m.sender[0]) ? LINE_H : 0);
  }
  _layoutValid = true;
}

void ChatScreen::drawBubble(App& app, lgfx::LovyanGFX& g, const StoredMsg& m, const Layout& l, int y) {
  const int x = m.outgoing ? SCREEN_W - 6 - l.width : 3;
  g.fillRoundRect(x, y, l.width, l.height, 4, m.outgoing ? BUBBLE_OUT : BUBBLE_IN);
  int ty = y + 2;
  if (!m.outgoing && m.sender[0]) {
    g.setFont(&fontBold);
    g.setTextColor(ACCENT);
    g.drawString(m.sender, x + 4, ty);
    ty += LINE_H;
  }
  g.setFont(&fontText);
  g.setTextColor(FG);
  char buf[MSG_WRAP_BUF];
  for (int k = 0; k < l.lines; k++) {
    memcpy(buf, m.text + l.line[k].start, l.line[k].len);
    buf[l.line[k].len] = 0;
    g.drawString(buf, x + 4, ty);
    ty += LINE_H;
  }

  // Meta line: time, then delivery status (outgoing) or reception quality (incoming)
  char when[12], meta[48];
  formatWhen(m.ts, nowUnix(app), utcOffset(app), when, sizeof(when));
  uint16_t statusColor = DIM;
  if (m.outgoing) {
    const char* st = "";
    switch (m.state) {
      case MSG_PENDING: st = "envoi..."; break;
      case MSG_SENT_FLOOD:
      case MSG_SENT_DIRECT: st = m.attempt > 1 ? "renvoyé" : "envoyé"; break;
      case MSG_DELIVERED: st = "reçu"; statusColor = COLOR_OK; break;
      case MSG_FAILED: st = "échec"; statusColor = COLOR_BAD; break;
      case MSG_CHANNEL_SENT: st = "envoyé"; break;
    }
    snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s", when, st);
  } else if (m.hops == 0xFF) {
    snprintf(meta, sizeof(meta), "%s", when);
  } else {
    char hops[16];
    hopsLabel(m.hops, hops, sizeof(hops));
    snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s \xC2\xB7 %.1f dB", when, hops, m.snrX4 / 4.0f);
  }
  g.setFont(&fontSmall);
  g.setTextColor(statusColor);
  g.setTextDatum(lgfx::top_right);
  g.drawString(meta, x + l.width - 4, ty);
  g.setTextDatum(lgfx::top_left);
}

void ChatScreen::drawCompose(App&, lgfx::LovyanGFX& g) {
  const int y = CONTENT_Y + CONTENT_H - COMPOSE_H;
  g.fillRect(0, y, SCREEN_W, COMPOSE_H, BG);
  g.drawRoundRect(1, y + 1, SCREEN_W - 2, COMPOSE_H - 1, 3, ACCENT);
  g.setFont(&fontText);
  size_t len = strlen(_draft);
  int counterW = 0;
  char counter[24];
  if ((int)len > _maxLen - 40) {
    snprintf(counter, sizeof(counter), "%u/%d", (unsigned)len, _maxLen);
    g.setFont(&fontSmall);
    counterW = g.textWidth(counter) + 4;
    g.setTextColor((int)len >= _maxLen ? COLOR_WARN : DIM);
    g.setTextDatum(lgfx::top_right);
    g.drawString(counter, SCREEN_W - 5, y + 4);
    g.setTextDatum(lgfx::top_left);
    g.setFont(&fontText);
  }
  if (!len) {
    g.setTextColor(DIM);
    g.drawString("Écrire un message...", 6, y + 2);
    if (_cursorOn) g.fillRect(5, y + 3, 2, LINE_H - 1, FG);
    return;
  }
  const int maxW = SCREEN_W - 14 - counterW;
  const char* p = _draft;
  while (*p && g.textWidth(p) > maxW) {   // show the end of a long draft
    p++;
    while ((*p & 0xC0) == 0x80) p++;
  }
  g.setTextColor(FG);
  g.drawString(p, 6, y + 2);
  if (_cursorOn) g.fillRect(7 + g.textWidth(p), y + 3, 2, LINE_H - 1, FG);
}

void ChatScreen::draw(App& app, lgfx::LovyanGFX& g) {
  if (!_layoutValid) layout(g);
  const int top = CONTENT_Y;
  int y = CONTENT_Y + CONTENT_H - COMPOSE_H - 2;
  if (_n == 0) {
    emptyState(g, "Pas encore de message", "Tape ton texte puis Entrée");
  }
  for (int i = _n - 1; i >= 0 && y > top; i--) {
    y -= _lay[i].height;
    drawBubble(app, g, _msgs[i], _lay[i], y);
    y -= 3;
  }
  // Scrollbar: position of the loaded window in the history
  uint32_t hiddenAbove = _total > (uint32_t)(_scroll + _n) ? _total - _scroll - _n : 0;
  if (_total > 1) drawScrollbar(g, hiddenAbove, _n, _total);
  if (_scroll > 0) {
    g.setFont(&fontSmall);
    g.setTextColor(COLOR_WARN);
    g.setTextDatum(lgfx::top_right);
    char b[24];
    snprintf(b, sizeof(b), "%d plus récent%s", _scroll, _scroll > 1 ? "s" : "");
    g.drawString(b, SCREEN_W - 6, CONTENT_Y + CONTENT_H - COMPOSE_H - 11);
    g.setTextDatum(lgfx::top_left);
  }
  drawCompose(app, g);
}

// =============================================================== ContactsScreen

const char* ContactsScreen::help() const {
  return _pick ? "Entrée:écrire  ;.:choisir  `:retour" : "M:écrire F:favori D:suppr. A:s'annoncer";
}

void ContactsScreen::onEnter(App&) {
  _list = ListState();
  _selKey = ConvKey();
}

void ContactsScreen::syncSelection(MeshService& mesh) {
  int n = mesh.contactCount();
  if (_selKey.valid()) {
    ContactView c;
    for (int i = 0; i < n; i++) {
      if (mesh.contactAt(i, c) && c.key == _selKey) { _list.sel = i; break; }
    }
  }
  _list.clamp(n, LIST_VISIBLE_ROWS);
}

void ContactsScreen::remember(MeshService& mesh) {
  ContactView c;
  _selKey = mesh.contactAt(_list.sel, c) ? c.key : ConvKey();
}

bool ContactsScreen::onKey(App& app, const KeyEvent& e) {
  MeshService& mesh = *app.services().mesh;
  syncSelection(mesh);
  int n = mesh.contactCount();
  ContactView c;
  bool has = n > 0 && mesh.contactAt(_list.sel, c);

  if (e.code == KeyCode::Esc) { _pick = false; return false; }
  if (e.code == KeyCode::Up) { _list.move(-1, n, LIST_VISIBLE_ROWS); remember(mesh); return true; }
  if (e.code == KeyCode::Down) { _list.move(1, n, LIST_VISIBLE_ROWS); remember(mesh); return true; }
  if (e.code == KeyCode::Enter && has) {
    if (_pick) {
      _pick = false;
      _chat.open(app, c.key, c.name);
      app.pop();            // replace the picker by the chat
      app.push(&_chat);
    } else {
      _detail.show(c.key);
      app.push(&_detail);
    }
    return true;
  }
  if (e.is('a')) {
    bool ok = mesh.sendAdvert(true, app.services().settings->get().shareLocation);
    app.notify(ok ? "Annonce envoyée" : "Annonce impossible", ok ? "Les autres nœuds vont te voir" : "Radio indisponible",
               2000);
    return true;
  }
  if (!has) return false;
  if (e.is('m')) {
    _chat.open(app, c.key, c.name);
    app.push(&_chat);
    return true;
  }
  if (e.is('f')) {
    _selKey = c.key;   // keep the cursor on this contact when it moves in the list
    mesh.setFavourite(c.key, !c.favourite);
    app.notify(c.name, c.favourite ? "Retiré des favoris" : "Ajouté aux favoris", 1500);
    return true;
  }
  if (e.is('d')) {
    if (_confirm.check(app, _list.sel, "Supprimer ce contact ?")) {
      _selKey = ConvKey();
      mesh.removeContact(c.key);
      app.notify(c.name, "Contact supprimé", 1500);
      _list.clamp(mesh.contactCount(), LIST_VISIBLE_ROWS);
    }
    return true;
  }
  return false;
}

bool ContactsScreen::onEvent(App&, const MeshEvent&) {
  return false;   // list refreshes on redraw; let the banner show new messages
}

void ContactsScreen::draw(App& app, lgfx::LovyanGFX& g) {
  MeshService& mesh = *app.services().mesh;
  int n = mesh.contactCount();
  snprintf(_title, sizeof(_title), _pick ? "Écrire à... (%d)" : "Contacts (%d)", n);
  if (n == 0) {
    emptyState(g, "Aucun contact pour l'instant", "A : s'annoncer sur le réseau");
    return;
  }
  syncSelection(mesh);
  DeviceStatus st = app.services().sys->status();
  uint32_t now = st.unixTime;
  for (int i = 0; i < LIST_VISIBLE_ROWS && _list.top + i < n; i++) {
    int k = _list.top + i;
    ContactView c;
    if (!mesh.contactAt(k, c)) break;
    int y = CONTENT_Y + 1 + i * LINE_H;
    char right[32], hops[16], ago[12], name[48];
    if (c.hasPos && st.hasPosition) {
      double d = geo::distanceM(st.lat, st.lon, c.latE6 / 1e6, c.lonE6 / 1e6);
      if (d < 1000) snprintf(hops, sizeof(hops), "%.0f m", d);
      else snprintf(hops, sizeof(hops), "%.1f km", d / 1000);
    } else {
      hopsLabel(c.hops, hops, sizeof(hops));
    }
    if (c.lastHeard && clockValid(now) && now >= c.lastHeard) {
      formatAgo(now - c.lastHeard, ago, sizeof(ago));
      snprintf(right, sizeof(right), "%s  %s", hops, ago);
    } else {
      snprintf(right, sizeof(right), "%s", hops);
    }
    snprintf(name, sizeof(name), "%s%s%s", c.favourite ? "* " : "", typePrefix(c.type), c.name);
    drawListRow(g, y, name, right, k == _list.sel, DIM, c.type != CONTACT_CHAT);
  }
  drawScrollbar(g, _list.top, LIST_VISIBLE_ROWS, n);
}

// =============================================================== ContactDetailScreen

bool ContactDetailScreen::onKey(App& app, const KeyEvent& e) {
  MeshService& mesh = *app.services().mesh;
  ContactView c;
  if (!mesh.findContact(_key, c)) return false;
  if (e.is('m')) {
    _chat.open(app, c.key, c.name);
    app.push(&_chat);
    return true;
  }
  if (e.is('f')) {
    mesh.setFavourite(c.key, !c.favourite);
    return true;
  }
  if (e.is('b') && _compass) {
    if (!c.hasPos) {
      app.notify(c.name, "Position inconnue (il ne la partage pas)", 2500);
      return true;
    }
    _compass->setTarget(c.key);
    app.push(_compass);
    return true;
  }
  if (e.is('x')) {
    mesh.resetPath(c.key);
    app.notify(c.name, "Chemin réinitialisé (prochain envoi en flood)", 2500);
    return true;
  }
  return false;
}

bool ContactDetailScreen::tick(App& app) {
  if (app.now() - _lastRefresh >= 1000) { _lastRefresh = app.now(); return true; }
  return false;
}

void ContactDetailScreen::draw(App& app, lgfx::LovyanGFX& g) {
  ContactView c;
  if (!app.services().mesh->findContact(_key, c)) {
    emptyState(g, "Contact introuvable", "` pour revenir");
    return;
  }
  DeviceStatus st = app.services().sys->status();
  char v[7][48];
  const char* labels[7] = {"Nom", "Type", "Clé", "Chemin", "Entendu", "Position", "Distance"};
  snprintf(v[0], 48, "%s%s", c.favourite ? "* " : "", c.name);
  snprintf(v[1], 48, "%s", typeName(c.type));
  snprintf(v[2], 48, "%02x%02x%02x%02x%02x%02x..", c.pubKey[0], c.pubKey[1], c.pubKey[2], c.pubKey[3], c.pubKey[4],
           c.pubKey[5]);
  if (c.hops < 0) snprintf(v[3], 48, "inconnu (flood)");
  else hopsLabel(c.hops, v[3], 48);
  if (c.lastHeard && clockValid(st.unixTime) && st.unixTime >= c.lastHeard) {
    char ago[12];
    formatAgo(st.unixTime - c.lastHeard, ago, sizeof(ago));
    snprintf(v[4], 48, "il y a %s", ago);
  } else {
    snprintf(v[4], 48, "jamais");
  }
  double lat = c.latE6 / 1e6, lon = c.lonE6 / 1e6;
  if (c.hasPos) snprintf(v[5], 48, "%.5f, %.5f", lat, lon);
  else snprintf(v[5], 48, "inconnue");
  if (c.hasPos && st.hasPosition) {
    double d = geo::distanceM(st.lat, st.lon, lat, lon);
    double b = geo::bearingDeg(st.lat, st.lon, lat, lon);
    if (d < 1000) snprintf(v[6], 48, "%.0f m  %s (%.0f\xC2\xB0)", d, geo::compassLabel(b), b);
    else snprintf(v[6], 48, "%.1f km  %s (%.0f\xC2\xB0)", d / 1000, geo::compassLabel(b), b);
  } else {
    snprintf(v[6], 48, c.hasPos ? "ma position ?" : "-");
  }
  for (int i = 0; i < 7; i++) {
    drawListRow(g, CONTENT_Y + 1 + i * LINE_H, labels[i], v[i], false, i == 0 ? ACCENT : FG, true);
  }
}

// =============================================================== ConversationsScreen

void ConversationsScreen::onJoinDone(App& app, const char* text, void*) {
  char name[32];
  if (text[0] == '#') utf8Copy(name, sizeof(name), text);
  else snprintf(name, sizeof(name), "#%s", text);
  if (strlen(name) < 2) return;
  bool ok = app.services().mesh->joinChannel(name);
  app.notify(name, ok ? "Canal ajouté" : "Impossible (trop de canaux ?)", 2000);
}

void ConversationsScreen::syncSelection(MessageStore& st) {
  if (_selKey.valid()) {
    for (int i = 0; i < st.count(); i++) {
      if (st.at(i)->key == _selKey) { _list.sel = i; break; }
    }
  }
  _list.clamp(st.count(), VISIBLE);
}

void ConversationsScreen::remember(MessageStore& st) {
  const ConvMeta* c = st.at(_list.sel);
  _selKey = c ? c->key : ConvKey();
}

bool ConversationsScreen::onKey(App& app, const KeyEvent& e) {
  MeshService& mesh = *app.services().mesh;
  MessageStore& st = mesh.messages();
  int n = st.count();
  syncSelection(st);
  if (e.code == KeyCode::Up) { _list.move(-1, n, VISIBLE); remember(st); return true; }
  if (e.code == KeyCode::Down) { _list.move(1, n, VISIBLE); remember(st); return true; }
  if (e.code == KeyCode::Enter && n) {
    const ConvMeta* c = st.at(_list.sel);
    if (c) {
      _chat.open(app, c->key, c->name);
      app.push(&_chat);
    }
    return true;
  }
  if (e.is('n')) {
    _contacts.setPickMode(true);
    app.push(&_contacts);
    return true;
  }
  if (e.is('j')) {
    _input.setup("Rejoindre un canal", "#", 30, onJoinDone, this);
    app.push(&_input);
    return true;
  }
  if (e.is('d') && n) {
    const ConvMeta* c = st.at(_list.sel);
    if (!c) return true;
    bool channel = c->key.kind == ConvKey::CHANNEL;
    if (_confirm.check(app, _list.sel, channel ? "Quitter ce canal ?" : "Effacer cette conversation ?")) {
      ConvKey k = c->key;
      if (channel) mesh.leaveChannel(k);
      else st.remove(k);
      _list.clamp(st.count(), VISIBLE);
    }
    return true;
  }
  return false;
}

bool ConversationsScreen::onEvent(App&, const MeshEvent&) {
  return false;   // the list is redrawn; the banner still announces the message
}

void ConversationsScreen::draw(App& app, lgfx::LovyanGFX& g) {
  MessageStore& st = app.services().mesh->messages();
  int n = st.count();
  if (n == 0) {
    emptyState(g, "Aucune conversation", "N : écrire   J : rejoindre un canal");
    return;
  }
  syncSelection(st);
  uint32_t now = nowUnix(app);
  int tz = utcOffset(app);
  char buf[96], line[96];
  for (int i = 0; i < VISIBLE && _list.top + i < n; i++) {
    const ConvMeta* c = st.at(_list.top + i);
    if (!c) break;
    const int y = CONTENT_Y + i * ROW_H;
    const bool sel = _list.top + i == _list.sel;
    if (sel) g.fillRect(0, y, SCREEN_W - 4, ROW_H, SELECT_BG);

    // line 1: name + time
    char when[12];
    when[0] = 0;
    if (c->total) formatWhen(c->lastTs, now, tz, when, sizeof(when));
    g.setFont(&fontSmall);
    g.setTextColor(DIM);
    g.setTextDatum(lgfx::top_right);
    g.drawString(when, SCREEN_W - 8, y + 2);
    int whenW = g.textWidth(when);
    g.setTextDatum(lgfx::top_left);
    g.setFont(c->unread ? &fontBold : &fontText);
    g.setTextColor(c->key.kind == ConvKey::CHANNEL ? ACCENT : FG);
    fitText(g, c->name, SCREEN_W - 16 - whenW, line, sizeof(line));
    g.drawString(line, MARGIN, y + 1);

    // line 2: preview + unread badge
    int badgeW = 0;
    if (c->unread) {
      snprintf(buf, sizeof(buf), "%u", c->unread);
      g.setFont(&fontSmall);
      badgeW = g.textWidth(buf) + 8;
      g.fillRoundRect(SCREEN_W - 8 - badgeW, y + LINE_H + 1, badgeW, 11, 5, COLOR_WARN);
      g.setTextColor(BG);
      g.setTextDatum(lgfx::top_center);
      g.drawString(buf, SCREEN_W - 8 - badgeW / 2, y + LINE_H + 2);
      g.setTextDatum(lgfx::top_left);
    }
    if (c->total) snprintf(buf, sizeof(buf), "%s%s", c->lastOutgoing ? "Moi : " : "", c->last);
    else snprintf(buf, sizeof(buf), "(aucun message)");
    g.setFont(&fontText);
    g.setTextColor(c->unread ? FG : DIM);
    fitText(g, buf, SCREEN_W - 14 - badgeW, line, sizeof(line));
    g.drawString(line, MARGIN + 6, y + LINE_H);
  }
  drawScrollbar(g, _list.top, VISIBLE, n);
}
