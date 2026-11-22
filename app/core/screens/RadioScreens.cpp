#include "RadioScreens.h"
#include "SettingsScreens.h"
#include "../drivers/FloodOptions.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
void announcePathId(App& app, const UiSettings& s) {
  char t[48];
  snprintf(t, sizeof(t), tr("Path ID size: %u bytes", "Taille ID chemin : %u octets"), (unsigned)s.pathHashBytes);
  app.notify(t, tr("This network's choice", "Le choix de ce réseau"), 3500);
}

void warnOutsideBand(App& app, float mhz) {
  if (RadioConfig::inCapBand(mhz)) return;
  app.notify(tr("Outside 868-923 MHz", "Hors 868-923 MHz"),
             tr("Short range with this Cap", "Portée faible avec ce Cap"), 4500);
}
}  // namespace

// =============================================================== RadioPresetScreen

void RadioPresetScreen::onEnter(App& app) {
  const UiSettings& s = app.services().settings->get();
  _list = ListState();
  for (int r = 0; r < ROWS; r++) {
    if (presetAt(r) == s.radioPreset) _list.sel = r;
  }
  _list.clamp(ROWS, LIST_VISIBLE_ROWS);
}

bool RadioPresetScreen::onKey(App& app, const KeyEvent& e) {
  switch (e.code) {
    case KeyCode::Up: _list.move(-1, ROWS, LIST_VISIBLE_ROWS); return true;
    case KeyCode::Down: _list.move(1, ROWS, LIST_VISIBLE_ROWS); return true;
    case KeyCode::Enter: {
      UiSettings s = app.services().settings->get();
      const uint8_t preset = presetAt(_list.sel);
      const bool pathChanged = RadioConfig::choose(s, preset);
      app.services().settings->save(s);
      if (preset == RADIO_PRESET_CUSTOM) {
        app.pop();
        app.push(&_custom);                 // the values come next
        return true;
      }
      if (pathChanged) announcePathId(app, s);
      app.pop();
      return true;
    }
    default:
      return false;
  }
}

void RadioPresetScreen::draw(App& app, lgfx::LovyanGFX& g) {
  const UiSettings& s = app.services().settings->get();
  _list.clamp(ROWS, LIST_VISIBLE_ROWS);
  char value[32];
  for (int i = 0; i < LIST_VISIBLE_ROWS && _list.top + i < ROWS; i++) {
    const int r = _list.top + i;
    const uint8_t preset = presetAt(r);
    const bool current = preset == s.radioPreset;
    const char* label;
    if (preset == RADIO_PRESET_CUSTOM) {
      label = tr("Custom...", "Perso...");
      snprintf(value, sizeof(value), "%s", current ? "*" : "");
    } else {
      const RadioPreset& p = RADIO_PRESETS[preset];
      label = p.name;
      snprintf(value, sizeof(value), "%s%.3f SF%u", current ? "* " : "", p.freqMHz, p.sf);
    }
    drawListRow(g, CONTENT_Y + 1 + i * LINE_H, label, value, r == _list.sel, current ? COLOR_OK : DIM);
  }
  drawScrollbar(g, _list.top, LIST_VISIBLE_ROWS, ROWS);
}

// =============================================================== RadioCustomScreen

void RadioCustomScreen::onEnter(App& app) {
  UiSettings s = app.services().settings->get();
  if (!RadioConfig::isCustom(s)) {          // opened from a preset: start from its values
    RadioConfig::setCustom(s, RadioConfig::of(s));
    app.services().settings->save(s);
  }
  _list = ListState();
}

void RadioCustomScreen::save(App& app, const RadioParams& p) {
  UiSettings s = app.services().settings->get();
  RadioConfig::setCustom(s, p);
  app.services().settings->save(s);
}

void RadioCustomScreen::change(App& app, int item, int delta) {
  RadioParams p = RadioConfig::of(app.services().settings->get());
  switch (item) {
    case FREQUENCY: {                        // fine steps with the arrows: 25 kHz
      const float f = p.freqMHz + delta * 0.025f;
      if (!RadioConfig::frequencyValid(f)) return;
      p.freqMHz = f;
      save(app, p);
      warnOutsideBand(app, f);
      return;
    }
    case BANDWIDTH: {
      const int n = RadioConfig::BANDWIDTH_COUNT;
      const int i = (RadioConfig::bandwidthIndex(p.bwKHz) + delta + n) % n;
      p.bwKHz = RadioConfig::BANDWIDTHS[i];
      break;
    }
    case SPREADING: p.sf = (uint8_t)(5 + (p.sf - 5 + delta + 8) % 8); break;
    case CODING: p.cr = (uint8_t)(5 + (p.cr - 5 + delta + 4) % 4); break;
    default: return;
  }
  save(app, p);
}

void RadioCustomScreen::onFrequencyDone(App& app, const char* text, void* ctx) {
  RadioCustomScreen* self = (RadioCustomScreen*)ctx;
  float mhz = 0;
  if (!RadioConfig::parseFrequency(text, mhz)) {
    app.notify(tr("Not a frequency", "Fréquence invalide"), tr("MHz, 150 to 960, e.g. 910.525", "MHz, 150 à 960, ex. 910,525"), 3500);
    return;
  }
  RadioParams p = RadioConfig::of(app.services().settings->get());
  p.freqMHz = mhz;
  self->save(app, p);
  warnOutsideBand(app, mhz);
}

bool RadioCustomScreen::onKey(App& app, const KeyEvent& e) {
  switch (e.code) {
    case KeyCode::Up:
      _list.move(-1, NOTE, LIST_VISIBLE_ROWS);   // the note is not selectable
      return true;
    case KeyCode::Down:
      _list.move(1, NOTE, LIST_VISIBLE_ROWS);
      return true;
    case KeyCode::Left: change(app, _list.sel, -1); return true;
    case KeyCode::Right: change(app, _list.sel, +1); return true;
    case KeyCode::Enter:
      if (_list.sel == FREQUENCY) {
        snprintf(_freqText, sizeof(_freqText), "%.3f", RadioConfig::of(app.services().settings->get()).freqMHz);
        _input.setup(tr("Frequency (MHz)", "Fréquence (MHz)"), _freqText, sizeof(_freqText) - 1, onFrequencyDone, this);
        app.push(&_input);
      } else {
        change(app, _list.sel, +1);
      }
      return true;
    default:
      return false;
  }
}

void RadioCustomScreen::draw(App& app, lgfx::LovyanGFX& g) {
  const RadioParams p = RadioConfig::of(app.services().settings->get());
  char v[32];
  const char* labels[COUNT] = {tr("Frequency", "Fréquence"), tr("Bandwidth", "Bande passante"), tr("Spreading factor", "Facteur d'étal."),
                               tr("Coding rate", "Taux de codage"), ""};
  for (int i = 0; i < NOTE; i++) {
    switch (i) {
      case FREQUENCY: snprintf(v, sizeof(v), "%.3f MHz", p.freqMHz); break;
      case BANDWIDTH: snprintf(v, sizeof(v), "< %g kHz >", p.bwKHz); break;
      case SPREADING: snprintf(v, sizeof(v), "< SF%u >", p.sf); break;
      case CODING: snprintf(v, sizeof(v), "< 4/%u >", p.cr); break;
    }
    const bool warn = i == FREQUENCY && !RadioConfig::inCapBand(p.freqMHz);
    drawListRow(g, CONTENT_Y + 1 + i * LINE_H, labels[i], v, i == _list.sel, warn ? COLOR_WARN : ACCENT);
  }
  // Every node of a mesh must use the same frequency, bandwidth and spreading factor
  g.setFont(&fontSmall);
  g.setTextColor(DIM);
  const int y = CONTENT_Y + 1 + NOTE * LINE_H + 4;
  g.drawString(tr("Frequency, bandwidth and SF must match", "Fréquence, bande et SF identiques"), MARGIN, y);
  g.drawString(tr("the other nodes exactly.", "à ceux des autres appareils."), MARGIN, y + 10);
  if (!RadioConfig::inCapBand(p.freqMHz)) {
    g.setTextColor(COLOR_WARN);
    g.drawString(tr("Outside 868-923 MHz: short range.", "Hors 868-923 MHz : faible portée."), MARGIN, y + 22);
  }
}
