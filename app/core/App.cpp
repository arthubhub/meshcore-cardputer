#include "App.h"
#include "Widgets.h"
#include <stdio.h>

using namespace theme;

void App::begin(Screen* root) {
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
  utf8Copy(_bannerText, sizeof(_bannerText), text ? text : "");
  _bannerUntil = now() + ms;
  if (!_bannerUntil) _bannerUntil = 1;
  _dirty = true;
}

bool App::pollEvents() {
  MeshEvent ev;
  bool any = false;
  int guard = 8;   // bounded work per loop
  while (guard-- && _svc.mesh->pollEvent(ev)) {
    any = true;
    _dirty = true;
    Screen* s = top();
    if (s && s->onEvent(*this, ev)) continue;
    switch (ev.type) {
      case MeshEvent::MESSAGE:
        notify(ev.title, ev.text);
        _svc.sys->playSound(ev.key.kind == ConvKey::DIRECT ? SOUND_DIRECT_MSG : SOUND_CHANNEL_MSG);
        break;
      case MeshEvent::FAILED: {
        char t[64];
        snprintf(t, sizeof(t), "Échec d'envoi : %s", ev.title);
        notify(t, "Pas d'accusé de réception", 3500);
        _svc.sys->playSound(SOUND_FAIL);
        break;
      }
      case MeshEvent::CONTACT_NEW:
        notify("Nouveau contact", ev.title, 2000);
        break;
      default:
        break;
    }
  }
  return any;
}

bool App::loop() {
  pollEvents();
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
