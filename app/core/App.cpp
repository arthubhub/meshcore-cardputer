#include "App.h"
#include "Widgets.h"
#include "Log.h"
#include <string.h>
#include <stdlib.h>
#include "drivers/NotifyPolicy.h"
#include "drivers/Mentions.h"
#include <stdio.h>

using namespace theme;

void App::loadThemes() {
  _themes.clearCustom();
  ThemeSource* src = _svc.themes;
  const int n = src ? src->count() : 0;
  if (!n) {
    _themeWanted[0] = 0;
    return;
  }
  char* text = (char*)malloc(2048);           // one theme file at a time, only while loading (boot)
  if (!text) {
    CORE_LOGE("THEME", "no memory to read the theme files");
    return;
  }
  for (int i = 0; i < n; i++) {
    char name[Palette::NAME_LEN + 1], err[80];
    int warnings = 0;
    if (!src->read(i, name, sizeof(name), text, 2048)) {
      CORE_LOGW("THEME", "theme file %d (%s) cannot be read", i, name);
      continue;
    }
    if (!_themes.add(text, name, err, sizeof(err), warnings)) {
      CORE_LOGW("THEME", "theme '%s' not loaded: %s", name, err);
      continue;
    }
    const Palette& p = _themes.at(_themes.count() - 1);
    if (warnings) CORE_LOGW("THEME", "theme '%s': %d line(s) ignored, first: %s", p.name, warnings, err);
    const char* weak[4];
    const int w = p.weakPairs(weak, 4);
    if (w) CORE_LOGW("THEME", "theme '%s': hard to read: %s%s", p.name, weak[0], w > 1 ? " and more" : "");
    CORE_LOGI("THEME", "theme '%s' loaded from the card", p.name);
  }
  free(text);
  _themeWanted[0] = 0;                        // applied again at the next loop
}

void App::begin(Screen* root) {
  i18n::set(_svc.settings->get().language);
  loadThemes();
  applyTheme();
  _depth = 0;
  push(root);
}

void App::push(Screen* s) {
  if (!s || _depth >= MAX_DEPTH) return;
  _stack[_depth++] = s;
  s->onEnter(*this);
  _dirty = true;
}

void App::pop() {
  if (_depth <= 1) return;  // never pop the root screen
  _depth--;
  top()->onResume(*this);
  _dirty = true;
}

void App::handleKey(const KeyEvent& raw) {
  Screen* s = top();
  if (!s) return;
  if (bannerVisible()) _bannerUntil = 0;   // any key dismisses the notification
  KeyEvent e = s->wantsText() ? raw : toNavigation(raw);
  bool used = s->onKey(*this, e);
  if (!used && e.code == KeyCode::Esc) pop();
  _dirty = true;
}

void App::notify(const char* title, const char* text, uint32_t ms) {
  utf8Copy(_bannerTitle, sizeof(_bannerTitle), title ? title : "");
  char shown[MSG_WRAP_BUF];
  MentionText::toDisplay(text ? text : "", shown, sizeof(shown), nullptr, 0);   // "@[name]" -> "@name"
  utf8Copy(_bannerText, sizeof(_bannerText), shown);
  _bannerUntil = now() + ms;
  if (!_bannerUntil) _bannerUntil = 1;
  _dirty = true;
}

bool App::pollEvents() {
  MeshEvent ev;
  bool any = false;
  int guard = 8;   // bounded work per loop
  const uint8_t mode = _svc.settings->get().wakeOn;
  while (guard-- && _svc.mesh->pollEvent(ev)) {
    any = true;
    _dirty = true;
    const Notice n = NotifyPolicy::decide(ev, mode);
    if (n.wake) _wake = true;
    Screen* s = top();
    if (s && s->onEvent(*this, ev)) continue;
    switch (ev.type) {
      case MeshEvent::MESSAGE:
        notify(ev.title, ev.text);
        break;
      case MeshEvent::MENTION: {
        char t[64];
        snprintf(t, sizeof(t), tr("@ you, %s", "@ toi, %s"), ev.title);
        notify(t, ev.text, 4000);
        break;
      }
      case MeshEvent::FAILED: {
        char t[64];
        snprintf(t, sizeof(t), tr("Not delivered: %s", "Échec d'envoi : %s"), ev.title);
        notify(t, tr("No acknowledgement received", "Pas d'accusé de réception"), 3500);
        break;
      }
      case MeshEvent::AIRTIME:
        notify(tr("Transmit time is high", "Temps d'émission élevé"), ev.text, 5000);
        break;
      case MeshEvent::CONTACTS_NEAR_FULL:
        notify(tr("Contacts almost full", "Contacts presque pleins"), ev.text, 4000);
        break;
      case MeshEvent::CONTACTS_REPLACING:
        notify(tr("Contact list full", "Liste de contacts pleine"),
               tr("Oldest replaced, favourites kept", "Les plus anciens sont remplacés"), 5000);
        break;
      case MeshEvent::CONTACTS_BLOCKED:
        notify(tr("New contact refused", "Nouveau contact refusé"),
               tr("All are favourites: remove one", "Tous favoris : en retirer un"), 5000);
        break;
      case MeshEvent::CONTACT_NEW:
        notify(tr("New contact", "Nouveau contact"), ev.title, 2000);
        break;
      default:
        break;
    }
    if (n.sound) _svc.sys->playSound(n.soundKind);
  }
  return any;
}

void App::chime(const MeshEvent& ev, SoundKind kind) {
  if (NotifyPolicy::soundOnScreen(ev, _svc.settings->get().wakeOn)) _svc.sys->playSound(kind);
}

void App::applyTheme() {
  const char* want = _svc.settings->get().theme;
  if (!strcmp(want, _themeWanted)) return;
  snprintf(_themeWanted, sizeof(_themeWanted), "%s", want);
  const Palette& p = _themes.find(want);
  theme::apply(p);
  if (strcmp(p.name, want) != 0) CORE_LOGW("THEME", "theme '%s' not found: %s instead", want, p.name);
  _dirty = true;
}

bool App::loop() {
  i18n::set(_svc.settings->get().language);   // follows the setting (cheap)
  applyTheme();                                // and the theme
  pollEvents();
  _wake = false;                               // the screen is on: nothing to wake
  if (_bannerUntil && !bannerVisible()) { _bannerUntil = 0; _dirty = true; }
  Screen* s = top();
  if (s && s->tick(*this)) _dirty = true;
  if (_dirty) {
    drawAll();
    return true;
  }
  // Status bar shows a clock: refresh it alone once per second (no content redraw:
  // the map would otherwise be re-read from the SD card every second).
  if (now() - _lastStatusDraw >= 1000) {
    _lastStatusDraw = now();
    _g.startWrite();
    drawStatusBar();
    _g.endWrite();
    return true;
  }
  return false;
}

void App::drawAll() {
  _dirty = false;
  _lastStatusDraw = now();
  _g.startWrite();
  _g.fillScreen(BG);
  if (Screen* s = top()) {
    _g.setClipRect(0, CONTENT_Y, SCREEN_W, CONTENT_H);
    s->draw(*this, _g);
    _g.clearClipRect();
  }
  if (bannerVisible()) drawBanner();
  drawStatusBar();
  drawHelpLine();
  _g.endWrite();
}

void App::drawBanner() {
  const int y = CONTENT_Y + 2, h = 2 * LINE_H + 4;
  _g.fillRoundRect(4, y, SCREEN_W - 8, h, 4, BAR_BG);
  _g.drawRoundRect(4, y, SCREEN_W - 8, h, 4, ACCENT);
  char line[96];
  _g.setTextDatum(lgfx::top_left);
  _g.setFont(&fontBold);
  _g.setTextColor(ACCENT);
  fitText(_g, _bannerTitle, SCREEN_W - 20, line, sizeof(line));
  _g.drawString(line, 10, y + 2);
  _g.setFont(&fontText);
  _g.setTextColor(FG);
  fitText(_g, _bannerText, SCREEN_W - 20, line, sizeof(line));
  _g.drawString(line, 10, y + 2 + LINE_H);
}

void App::drawStatusBar() {
  DeviceStatus st = _svc.sys->status();
  _g.fillRect(0, 0, SCREEN_W, STATUS_H, BAR_BG);
  _g.setFont(&fontSmall);
  _g.setTextDatum(lgfx::top_left);

  // Left: local time (from GPS / phone), or --:--
  char buf[24];
  formatClock(st.unixTime, _svc.settings->get().utcOffsetMinutes, buf, sizeof(buf));
  _g.setTextColor(FG);
  _g.drawString(buf, 2, 1);

  // Then the current screen title
  if (Screen* s = top()) {
    _g.setTextColor(ACCENT);
    _g.drawString(s->title(), 36, 1);
  }

  // Right side, drawn right-to-left: battery, BLE, GPS, unread
  int x = SCREEN_W - 2;
  _g.setTextDatum(lgfx::top_right);

  if (st.batteryPercent >= 0) snprintf(buf, sizeof(buf), "%d%%%s", st.batteryPercent, st.charging ? "+" : "");
  else snprintf(buf, sizeof(buf), "?%%");
  _g.setTextColor(st.batteryPercent >= 0 && st.batteryPercent < 15 ? COLOR_BAD : FG);
  _g.drawString(buf, x, 1);
  x -= _g.textWidth(buf) + 6;

  if (st.bleEnabled) {
    _g.setTextColor(st.bleConnected ? ACCENT : DIM);
    _g.drawString("BLE", x, 1);
    x -= _g.textWidth("BLE") + 6;
  }

  if (st.gpsFix) snprintf(buf, sizeof(buf), "GPS%d", st.gpsSatellites);
  else snprintf(buf, sizeof(buf), "GPS-");
  _g.setTextColor(st.gpsFix ? COLOR_OK : DIM);
  _g.drawString(buf, x, 1);
  x -= _g.textWidth(buf) + 6;

  // Silent mode: a bell, crossed out when nothing wakes the screen, with "@" when only what is for me does
  const uint8_t wake = _svc.settings->get().wakeOn;
  if (wake == WAKE_NEVER || wake == WAKE_TO_ME) {
    const int bx = x - 7, by = 2;
    const uint16_t c = wake == WAKE_NEVER ? COLOR_WARN : DIM;
    _g.fillRoundRect(bx + 1, by, 5, 6, 2, c);   // the bell
    _g.drawFastHLine(bx, by + 6, 7, c);
    _g.fillRect(bx + 3, by + 7, 1, 1, c);       // its clapper
    if (wake == WAKE_NEVER) _g.drawLine(bx - 1, by + 8, bx + 7, by - 1, COLOR_BAD);
    x -= 10;
    if (wake == WAKE_TO_ME) {
      _g.setTextColor(DIM);
      _g.drawString("@", x, 1);
      x -= _g.textWidth("@") + 4;
    }
  }

  int unread = _svc.mesh->unreadCount();
  if (unread > 0) {
    snprintf(buf, sizeof(buf), "*%d", unread);
    _g.setTextColor(COLOR_WARN);
    _g.drawString(buf, x, 1);
  }
  _g.setTextDatum(lgfx::top_left);
}

void App::drawHelpLine() {
  const int y = SCREEN_H - HELP_H;
  _g.fillRect(0, y, SCREEN_W, HELP_H, BAR_BG);
  if (Screen* s = top()) {
    _g.setFont(&fontSmall);
    _g.setTextColor(DIM);
    char line[96];
    fitText(_g, s->help(), SCREEN_W - 4, line, sizeof(line));
    _g.drawString(line, 2, y + 1);
  }
}
