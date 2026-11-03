#include "SettingsScreens.h"
#include "../RadioPresets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace theme;

// ---------------------------------------------------------------- TextInputScreen

void TextInputScreen::setup(const char* title, const char* initial, size_t maxBytes, DoneFn done, void* ctx) {
  _title = title;
  _max = maxBytes < sizeof(_buf) - 1 ? maxBytes : sizeof(_buf) - 1;
  _buf[0] = 0;
  utf8Append(_buf, _max + 1, initial ? initial : "");
  _done = done;
  _ctx = ctx;
  _cursorOn = true;
}

bool TextInputScreen::onKey(App& app, const KeyEvent& e) {
  _cursorOn = true;
  _lastBlink = app.now();
  switch (e.code) {
    case KeyCode::Enter:
      if (_done) _done(app, _buf, _ctx);
      app.pop();
      return true;
    case KeyCode::Backspace:
      utf8PopBack(_buf);
      return true;
    case KeyCode::Char:
      if (e.mods & MOD_ALT) {
        composeAccent(_buf, _max + 1, e.ch);
      } else if (e.ch >= 0x20 && e.ch < 0x7F) {
        char s[2] = {e.ch, 0};
        utf8Append(_buf, _max + 1, s);
      }
      return true;
    default:
      return false;   // Esc (Fn+`) falls through: App pops the screen = cancel
  }
}

bool TextInputScreen::tick(App& app) {
  if (app.now() - _lastBlink >= 500) {
    _lastBlink = app.now();
    _cursorOn = !_cursorOn;
    return true;
  }
  return false;
}

void TextInputScreen::draw(App&, lgfx::LovyanGFX& g) {
  const int boxY = CONTENT_Y + 30, boxH = LINE_H + 8;
  g.setFont(&fontBold);
  g.setTextColor(ACCENT);
  g.drawString(_title, MARGIN, CONTENT_Y + 8);

  g.drawRect(MARGIN, boxY, SCREEN_W - 2 * MARGIN, boxH, ACCENT);
  g.setFont(&fontText);
  g.setTextColor(FG);
  // Show the end of the text if it is wider than the box
  const int maxW = SCREEN_W - 2 * MARGIN - 12;
  const char* p = _buf;
  while (*p && g.textWidth(p) > maxW) {
    p++;
    while ((*p & 0xC0) == 0x80) p++;
  }
  g.drawString(p, MARGIN + 4, boxY + 4);
  if (_cursorOn) {
    int cx = MARGIN + 4 + g.textWidth(p) + 1;
    g.fillRect(cx, boxY + 4, 2, LINE_H - 1, FG);
  }

  char info[24];
  snprintf(info, sizeof(info), "%u/%u", (unsigned)strlen(_buf), (unsigned)_max);
  g.setFont(&fontSmall);
  g.setTextColor(DIM);
  g.setTextDatum(lgfx::top_right);
  g.drawString(info, SCREEN_W - MARGIN, boxY + boxH + 3);
  g.setTextDatum(lgfx::top_left);
  g.drawString(tr("Accents: Alt+e, Alt+a, Alt+c...", "Accents : Alt+e, Alt+a, Alt+c..."), MARGIN, boxY + boxH + 3);
}

// ---------------------------------------------------------------- SystemScreen

namespace {
constexpr int SYSTEM_ROWS = 12;
}

bool SystemScreen::onKey(App&, const KeyEvent& e) {
  if (e.code == KeyCode::Up) { _list.move(-1, SYSTEM_ROWS, LIST_VISIBLE_ROWS); return true; }
  if (e.code == KeyCode::Down) { _list.move(1, SYSTEM_ROWS, LIST_VISIBLE_ROWS); return true; }
  return false;
}

bool SystemScreen::tick(App& app) {
  if (app.now() - _lastRefresh >= 1000) { _lastRefresh = app.now(); return true; }
  return false;
}

void SystemScreen::draw(App& app, lgfx::LovyanGFX& g) {
  SystemService& sys = *app.services().sys;
  RadioStats rs = app.services().mesh->radioStats();
  uint32_t up = sys.millis() / 1000;
  char v[SYSTEM_ROWS][40];
  const char* labels[SYSTEM_ROWS] = {"Version", tr("Free memory", "Mémoire libre"), tr("Lowest memory", "Mémoire min"),
                                     tr("Uptime", "Allumé depuis"), tr("Battery", "Batterie"),
                                     tr("Packets received", "Paquets reçus"), tr("Packets sent", "Paquets émis"),
                                     tr("Radio errors", "Erreurs radio"), tr("Last RSSI", "Dernier RSSI"),
                                     tr("Last SNR", "Dernier SNR"), "Radio", tr("SD card", "Carte SD")};
  snprintf(v[0], 40, "%s", sys.firmwareVersion());
  snprintf(v[1], 40, tr("%u KB", "%u Ko"), (unsigned)(sys.freeHeap() / 1024));
  snprintf(v[2], 40, tr("%u KB", "%u Ko"), (unsigned)(sys.minFreeHeap() / 1024));
  snprintf(v[3], 40, tr("%ud %02uh%02um%02us", "%uj %02uh%02um%02us"), (unsigned)(up / 86400), (unsigned)(up / 3600 % 24),
           (unsigned)(up / 60 % 60), (unsigned)(up % 60));
  int pct = sys.status().batteryPercent;
  if (pct >= 0) snprintf(v[4], 40, "%d %% (%u mV)", pct, (unsigned)sys.batteryMilliVolts());
  else snprintf(v[4], 40, "%u mV", (unsigned)sys.batteryMilliVolts());
  snprintf(v[5], 40, "%u", (unsigned)rs.rxPackets);
  snprintf(v[6], 40, "%u", (unsigned)rs.txPackets);
  snprintf(v[7], 40, "%u", (unsigned)rs.rxErrors);
  snprintf(v[8], 40, "%.0f dBm", rs.lastRssi);
  snprintf(v[9], 40, "%.1f dB", rs.lastSnr);
  snprintf(v[10], 40, "%.3f SF%u", rs.freqMHz, rs.spreadingFactor);
  BackupService* bk = app.services().backup;
  TileSource* tl = app.services().tiles;
  if (!bk || !bk->available()) snprintf(v[11], 40, "%s", tr("missing", "absente"));
  else snprintf(v[11], 40, "ok%s", tl ? (tl->available() ? tr(", map ok", ", carte ok") : tr(", no map", ", sans carte")) : "");

  _list.clamp(SYSTEM_ROWS, LIST_VISIBLE_ROWS);
  for (int i = 0; i < LIST_VISIBLE_ROWS && _list.top + i < SYSTEM_ROWS; i++) {
    int k = _list.top + i;
    drawListRow(g, CONTENT_Y + 1 + i * LINE_H, labels[k], v[k], k == _list.sel, FG);
  }
  drawScrollbar(g, _list.top, LIST_VISIBLE_ROWS, SYSTEM_ROWS);
}

// ---------------------------------------------------------------- SettingsScreen

namespace {
const uint16_t TIMEOUTS[] = {15, 30, 60, 120, 300, 0};
constexpr int TIMEOUT_COUNT = sizeof(TIMEOUTS) / sizeof(TIMEOUTS[0]);
// Row labels in the UI language. "Language / Langue" stays bilingual so it can be found in either.
const char* label(int item) {
  switch (item) {
    case SettingsScreen::LANGUAGE: return "Language / Langue";
    case SettingsScreen::NAME: return tr("Name", "Nom");
    case SettingsScreen::RADIO: return tr("Radio preset", "Réglage radio");
    case SettingsScreen::RADIO_INFO: return tr("  Details", "  Détail");
    case SettingsScreen::TX_POWER: return tr("TX power", "Puissance émission");
    case SettingsScreen::TIMEZONE: return tr("Time zone", "Fuseau horaire");
    case SettingsScreen::SCREEN_TIMEOUT: return tr("Screen timeout", "Veille écran");
    case SettingsScreen::BRIGHTNESS: return tr("Brightness", "Luminosité");
    case SettingsScreen::SOUND: return tr("Sound", "Son");
    case SettingsScreen::SHARE_LOCATION: return tr("Share location", "Partager position");
    case SettingsScreen::BACKUP: return tr("Back up to SD", "Sauvegarder sur SD");
    case SettingsScreen::RESTORE: return tr("Restore from SD", "Restaurer depuis SD");
    case SettingsScreen::PHONE_MODE: return tr("Phone mode", "Mode téléphone");
    case SettingsScreen::SYSTEM: return tr("System", "Système");
    default: return "";
  }
}
}  // namespace

void SettingsScreen::onEnter(App&) {}

void SettingsScreen::onNameDone(App& app, const char* text, void*) {
  if (!*text) return;   // empty name: keep the old one
  UiSettings s = app.services().settings->get();
  strncpy(s.nodeName, text, sizeof(s.nodeName) - 1);
  s.nodeName[sizeof(s.nodeName) - 1] = 0;
  app.services().settings->save(s);
}

void SettingsScreen::change(App& app, int item, int delta) {
  UiSettings s = app.services().settings->get();
  switch (item) {
    case RADIO:
      s.radioPreset = (s.radioPreset + delta + RADIO_PRESET_COUNT) % RADIO_PRESET_COUNT;
      break;
    case TX_POWER: {
      int p = s.txPowerDbm + delta;
      if (p < 2) p = 2;
      if (p > 22) p = 22;
      s.txPowerDbm = p;
      break;
    }
    case TIMEZONE: {
      int m = s.utcOffsetMinutes + delta * 60;
      if (m < -12 * 60) m = -12 * 60;
      if (m > 14 * 60) m = 14 * 60;
      s.utcOffsetMinutes = m;
      break;
    }
    case SCREEN_TIMEOUT: {
      int i = 0;
      while (i < TIMEOUT_COUNT && TIMEOUTS[i] != s.screenTimeoutSecs) i++;
      if (i == TIMEOUT_COUNT) i = 2;
      s.screenTimeoutSecs = TIMEOUTS[(i + delta + TIMEOUT_COUNT) % TIMEOUT_COUNT];
      break;
    }
    case BRIGHTNESS: s.brightness = (s.brightness + delta + 3) % 3; break;
    case SOUND: s.sound = !s.sound; break;
    case SHARE_LOCATION: s.shareLocation = !s.shareLocation; break;
    case LANGUAGE:
      s.language = (uint8_t)((s.language + delta + LANG_COUNT) % LANG_COUNT);
      i18n::set(s.language);   // immediately, for the redraw that follows
      break;
    default: return;
  }
  app.services().settings->save(s);
}

bool SettingsScreen::onKey(App& app, const KeyEvent& e) {
  switch (e.code) {
    case KeyCode::Up:
      _list.move(-1, COUNT, LIST_VISIBLE_ROWS);
      if (_list.sel == RADIO_INFO) _list.move(-1, COUNT, LIST_VISIBLE_ROWS);   // info row is not selectable
      return true;
    case KeyCode::Down:
      _list.move(1, COUNT, LIST_VISIBLE_ROWS);
      if (_list.sel == RADIO_INFO) _list.move(1, COUNT, LIST_VISIBLE_ROWS);
      return true;
    case KeyCode::Left: change(app, _list.sel, -1); return true;
    case KeyCode::Right: change(app, _list.sel, +1); return true;
    case KeyCode::Enter:
      if (_list.sel == NAME) {
        _input.setup(tr("Device name", "Nom de l'appareil"), app.services().settings->get().nodeName,
                     sizeof(UiSettings::nodeName) - 1, onNameDone, this);
        app.push(&_input);
      } else if (_list.sel == SYSTEM) {
        app.push(&_system);
      } else if (_list.sel == PHONE_MODE) {
        SystemService* sys = app.services().sys;
        if (!sys->companionInstalled()) {
          app.notify(tr("Phone mode not installed", "Mode téléphone absent"), tr("Flash firmware-dual.bin", "Flasher firmware-dual.bin"), 3500);
          return true;
        }
        // Restarts the device into the phone firmware: ask twice
        if (!(_confirmItem == PHONE_MODE && (int32_t)(app.now() - _confirmUntil) < 0)) {
          _confirmItem = PHONE_MODE;
          _confirmUntil = app.now() + 3000;
          app.notify(tr("Switch to phone mode?", "Passer en mode téléphone ?"), tr("Enter again to restart", "Entrée encore pour redémarrer"), 3000);
          return true;
        }
        _confirmItem = -1;
        app.notify(tr("Phone mode", "Mode téléphone"), tr("To come back: press esc 3 times", "Retour : touche esc 3 fois"), 5000);
        sys->requestCompanionBoot();
      } else if (_list.sel == BACKUP || _list.sel == RESTORE) {
        BackupService* b = app.services().backup;
        if (!b || !b->available()) {
          app.notify(tr("No SD card", "Pas de carte SD"), tr("Insert a microSD card (FAT32)", "Insérer une carte microSD (FAT32)"), 2500);
          return true;
        }
        bool restore = _list.sel == RESTORE;
        // Restoring replaces the identity and contacts: ask twice
        if (restore && !(_confirmItem == RESTORE && (int32_t)(app.now() - _confirmUntil) < 0)) {
          _confirmItem = RESTORE;
          _confirmUntil = app.now() + 3000;
          app.notify(tr("Replace the configuration?", "Remplacer la configuration ?"), tr("Enter again to confirm", "Entrée encore pour confirmer"), 3000);
          return true;
        }
        _confirmItem = -1;
        char msg[64];
        bool ok = restore ? b->importConfig(msg, sizeof(msg)) : b->exportConfig(msg, sizeof(msg));
        app.notify(ok ? (restore ? tr("Configuration restored", "Configuration restaurée") : tr("Backup done", "Sauvegarde faite"))
                      : tr("Failed", "Échec"), msg, 3500);
      } else {
        change(app, _list.sel, +1);
      }
      return true;
    default:
      return false;
  }
}

void SettingsScreen::formatValue(App& app, int item, char* out, size_t n) {
  const UiSettings& s = app.services().settings->get();
  const RadioPreset& p = RADIO_PRESETS[s.radioPreset < RADIO_PRESET_COUNT ? s.radioPreset : 0];
  out[0] = 0;
  switch (item) {
    case LANGUAGE: snprintf(out, n, "< %s >", i18n::name(s.language)); break;
    case NAME: snprintf(out, n, "%s", s.nodeName); break;
    case RADIO: snprintf(out, n, "< %s >", p.name); break;
    case RADIO_INFO: snprintf(out, n, "%.3f BW%g SF%u", p.freqMHz, p.bwKHz, p.sf); break;
    case TX_POWER: snprintf(out, n, "< %d dBm >", s.txPowerDbm); break;
    case TIMEZONE:
      if (s.utcOffsetMinutes % 60) snprintf(out, n, "< UTC%+d:%02d >", s.utcOffsetMinutes / 60, abs(s.utcOffsetMinutes % 60));
      else snprintf(out, n, "< UTC%+d >", s.utcOffsetMinutes / 60);
      break;
    case SCREEN_TIMEOUT:
      if (s.screenTimeoutSecs == 0) snprintf(out, n, "< %s >", tr("never", "jamais"));
      else if (s.screenTimeoutSecs < 60) snprintf(out, n, "< %u s >", s.screenTimeoutSecs);
      else snprintf(out, n, "< %u min >", s.screenTimeoutSecs / 60);
      break;
    case BRIGHTNESS: {
      const char* L[] = {tr("low", "faible"), tr("medium", "moyenne"), tr("high", "forte")};
      snprintf(out, n, "< %s >", L[s.brightness < 3 ? s.brightness : 1]);
      break;
    }
    case SOUND: snprintf(out, n, "%s", s.sound ? tr("yes", "oui") : tr("no", "non")); break;
    case SHARE_LOCATION: snprintf(out, n, "%s", s.shareLocation ? tr("yes", "oui") : tr("no", "non")); break;
    case BACKUP:
    case RESTORE: {
      BackupService* b = app.services().backup;
      snprintf(out, n, "%s", b && b->available() ? ">" : tr("(no SD)", "(pas de SD)"));
      break;
    }
    case PHONE_MODE: snprintf(out, n, "%s", app.services().sys->companionInstalled() ? "Bluetooth >" : tr("(not installed)", "(non installé)")); break;
    case SYSTEM: snprintf(out, n, ">"); break;
  }
}

void SettingsScreen::draw(App& app, lgfx::LovyanGFX& g) {
  _list.clamp(COUNT, LIST_VISIBLE_ROWS);
  char value[48];
  for (int i = 0; i < LIST_VISIBLE_ROWS && _list.top + i < COUNT; i++) {
    int k = _list.top + i;
    formatValue(app, k, value, sizeof(value));
    BackupService* b = app.services().backup;
    bool info = (k == RADIO_INFO) || ((k == BACKUP || k == RESTORE) && !(b && b->available())) ||
                (k == PHONE_MODE && !app.services().sys->companionInstalled());
    drawListRow(g, CONTENT_Y + 1 + i * LINE_H, label(k), value, k == _list.sel,
                info ? DIM : ACCENT, info);
  }
  drawScrollbar(g, _list.top, LIST_VISIBLE_ROWS, COUNT);
}
