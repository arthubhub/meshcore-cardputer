#pragma once
// Radio presets outside the EU: the list of every preset (Settings > Radio preset, Enter), and the user's own values
// (the last entry, "Custom..."). Parameters go through core/drivers/RadioConfig.
#include "../App.h"
#include "../Widgets.h"
#include "../drivers/RadioConfig.h"

class TextInputScreen;
class RadioCustomScreen;

class RadioPresetScreen : public Screen {
public:
  RadioPresetScreen(RadioCustomScreen& custom) : _custom(custom) {}
  const char* title() const override { return tr("Radio preset", "Réglage radio"); }
  const char* help() const override { return tr(";. choose  Enter: use  `: back", ";. choisir  Entrée: utiliser  `: retour"); }
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  static constexpr int ROWS = RADIO_PRESET_COUNT + 1;   // + Custom
  uint8_t presetAt(int row) const { return row < RADIO_PRESET_COUNT ? RADIO_PRESET_ORDER[row] : RADIO_PRESET_CUSTOM; }
  RadioCustomScreen& _custom;
  ListState _list;
};

class RadioCustomScreen : public Screen {
public:
  explicit RadioCustomScreen(TextInputScreen& input) : _input(input) {}
  const char* title() const override { return tr("Custom radio", "Radio perso"); }
  const char* help() const override { return tr(";. select  ,/ change  Enter: edit", ";. choisir  ,/ changer  Entrée: modif."); }
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

  enum Item { FREQUENCY, BANDWIDTH, SPREADING, CODING, NOTE, COUNT };

private:
  void change(App& app, int item, int delta);
  void save(App& app, const RadioParams& p);
  static void onFrequencyDone(App& app, const char* text, void* ctx);

  TextInputScreen& _input;
  ListState _list;
  char _freqText[16] = {};
};
