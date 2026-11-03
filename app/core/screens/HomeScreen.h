#pragma once
#include "../App.h"

// Shown for screens that are planned but not built yet (later phases).
class PlaceholderScreen : public Screen {
public:
  PlaceholderScreen(const char* title, const char* phase) : _title(title), _phase(phase) {}
  const char* title() const override { return _title; }
  void draw(App&, lgfx::LovyanGFX& g) override;
private:
  const char* _title;
  const char* _phase;
};

class HomeScreen : public Screen {
public:
  HomeScreen(Screen* messages, Screen* contacts, Screen* gps, Screen* map, Screen* settings)
    : _messages(messages), _contacts(contacts), _gps(gps), _map(map), _settings(settings) {}
  const char* title() const override { return "Accueil"; }
  const char* help() const override {
    return _map ? "M:msg C:contacts G:gps K:carte S:régl." : "M:messages C:contacts G:gps S:réglages";
  }
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;
private:
  Screen *_messages, *_contacts, *_gps, *_map, *_settings;
  int _lastUnread = -1;
  uint32_t _lastRefresh = 0;
};
