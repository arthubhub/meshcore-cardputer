#pragma once
#include "../App.h"
#include "../Widgets.h"

// Single-line text entry (UTF-8). Enter validates, Fn+` cancels, Alt+letter types accents.
class TextInputScreen : public Screen {
public:
  using DoneFn = void (*)(App& app, const char* text, void* ctx);

  // Prepares the screen; call before App::push().
  void setup(const char* title, const char* initial, size_t maxBytes, DoneFn done, void* ctx);

  const char* title() const override { return _title; }
  const char* help() const override { return tr("Enter ok  Fn+` cancel  Alt+e é è ê", "Entrée ok  Fn+` annuler  Alt+e é è ê"); }
  bool wantsText() const override { return true; }
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  const char* _title = "";
  char _buf[96] = {};
  size_t _max = 31;
  DoneFn _done = nullptr;
  void* _ctx = nullptr;
  bool _cursorOn = true;
  uint32_t _lastBlink = 0;
};

class SystemScreen : public Screen {
public:
  const char* title() const override { return tr("System", "Système"); }
  const char* help() const override { return tr("; . scroll  ` back", "; . défiler  ` retour"); }
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;
private:
  ListState _list;
  uint32_t _lastRefresh = 0;
};

class PacketsScreen;

class SettingsScreen : public Screen {
public:
  SettingsScreen(TextInputScreen& input, SystemScreen& system, PacketsScreen& packets)
    : _input(input), _system(system), _packets(packets) {}
  const char* title() const override { return tr("Settings", "Réglages"); }
  const char* help() const override { return tr(";. select  ,/ change  Enter: edit", ";. choisir  ,/ changer  Entrée: modif."); }
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

  enum Item { LANGUAGE, NAME, RADIO, RADIO_INFO, TX_POWER, REGION, PATH_ID, TIMEZONE, SCREEN_TIMEOUT, BRIGHTNESS, SOUND,
              SHARE_LOCATION, BACKUP, RESTORE, PACKETS, PHONE_MODE, SYSTEM, COUNT };

private:
  void change(App& app, int item, int delta);
  void formatValue(App& app, int item, char* out, size_t n);
  static void onNameDone(App& app, const char* text, void* ctx);
  static void onRegionDone(App& app, const char* text, void* ctx);

  TextInputScreen& _input;
  SystemScreen& _system;
  PacketsScreen& _packets;
  ListState _list;
  uint32_t _confirmUntil = 0;
  int _confirmItem = -1;
};
