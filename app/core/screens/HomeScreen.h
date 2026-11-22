#pragma once
// The home screen as a navigation menu: a dashboard, then one page per section (Messages, Contacts, GPS, Map, Nearby,
// Packets, Settings...), with left / right sliding from page to page, Enter opening the section, and Down opening the
// same sections as a list (MenuScreen). The letters of the earlier home (M, C, G, K, N, S) still open their sections
// from any page. Sections are registered: a new part of the firmware adds its page with addSection().
#include "../App.h"
#include "../drivers/Carousel.h"
#include "Icons.h"

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

// A section of the menu. `summary` writes up to 3 short lines about it (counts, state); nullptr: none.
struct NavSection {
  using Summary = void (*)(App& app, char lines[3][48]);
  NavSection() {}
  NavSection(const char* en, const char* fr, char k, NavIcon i, Screen* s, Summary sum, const char* missEn = nullptr,
             const char* missFr = nullptr)
      : titleEn(en), titleFr(fr), key(k), icon(i), screen(s), summary(sum), missingEn(missEn), missingFr(missFr) {}
  const char* titleEn = "";
  const char* titleFr = "";
  char key = 0;                // its letter on the home screen, 0: none
  NavIcon icon = NavIcon::HOME;
  Screen* screen = nullptr;    // nullptr: not in this firmware (the map of a build without it)
  Summary summary = nullptr;
  const char* missingEn = nullptr;   // shown when `screen` is nullptr
  const char* missingFr = nullptr;
  const char* title() const { return tr(titleEn, titleFr); }
};

class HomeScreen;

// The sections as a list (Down from the home screen)
class MenuScreen : public Screen {
public:
  explicit MenuScreen(HomeScreen& home) : _home(home) {}
  const char* title() const override { return tr("Menu", "Menu"); }
  const char* help() const override { return tr(";. choose  Enter: open  `: back", ";. choisir  Entrée: ouvrir  `: retour"); }
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  static constexpr int ROW_H = 18;
  HomeScreen& _home;
  int _sel = 0, _top = 0;
};

class HomeScreen : public Screen {
public:
  static constexpr int MAX_SECTIONS = 10;

  HomeScreen(Screen* messages, Screen* contacts, Screen* gps, Screen* map, Screen* nearby, Screen* settings);
  // Adds a page before Settings (which stays last)
  void addSection(const NavSection& s);
  int sectionCount() const { return _n; }
  const NavSection& section(int i) const { return _s[i]; }
  // The summary of a built-in kind of section (Messages, Contacts, GPS, Map, Nearby, Packets, Settings)
  static NavSection::Summary defaultSummary(NavIcon icon);
  // Opens section i (a message when it is not in this firmware)
  void open(App& app, int i);

  const char* title() const override { return tr("Home", "Accueil"); }
  const char* help() const override;
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  void drawPage(App& app, lgfx::LovyanGFX& g, int page, int ox);
  void drawDashboard(App& app, lgfx::LovyanGFX& g, int ox);
  void drawCard(App& app, lgfx::LovyanGFX& g, const NavSection& s, int ox);
  void drawDots(lgfx::LovyanGFX& g);

  NavSection _s[MAX_SECTIONS];
  int _n = 0;
  Carousel _carousel;          // page 0: the dashboard, page i + 1: section i
  MenuScreen _menu{*this};
  int _lastUnread = -1;
  uint32_t _lastRefresh = 0;
};
