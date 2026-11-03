#pragma once
// UI application: screen stack, status bar, key routing, redraw scheduling.
// Portable: runs unchanged on the Cardputer and in the simulator.
#include "Keys.h"
#include "Services.h"
#include "Theme.h"

class App;

class Screen {
public:
  virtual ~Screen() {}
  virtual const char* title() const = 0;
  // Key hint shown on the bottom line (UTF-8, Latin-1 range).
  virtual const char* help() const { return "` retour"; }
  // true: the screen receives raw characters (text entry), arrows need Fn.
  virtual bool wantsText() const { return false; }
  virtual void onEnter(App&) {}    // pushed (opened)
  virtual void onResume(App&) {}   // visible again after the screen above was closed
  // Return true if the key was consumed. Esc not consumed => App pops the screen.
  virtual bool onKey(App&, const KeyEvent&) { return false; }
  // Draw the content area: y in [theme::CONTENT_Y, theme::CONTENT_Y + theme::CONTENT_H).
  virtual void draw(App&, lgfx::LovyanGFX& g) = 0;
  // Called every loop; return true to request a redraw (e.g. live data changed).
  virtual bool tick(App&) { return false; }
  // Mesh event (new message, delivery...). Return true if the screen handled it itself;
  // otherwise the App shows a notification banner.
  virtual bool onEvent(App&, const MeshEvent&) { return false; }
};

class App {
public:
  static constexpr int MAX_DEPTH = 8;

  App(Services svc, lgfx::LovyanGFX& canvas) : _svc(svc), _g(canvas) {}

  void begin(Screen* root);
  // Feed one key press. Returns true if it caused a redraw request.
  void handleKey(const KeyEvent& raw);
  // Call often. Returns true if the canvas was redrawn this call (caller then pushes it to the panel).
  bool loop();

  void push(Screen* s);
  void pop();
  Screen* top() const { return _depth ? _stack[_depth - 1] : nullptr; }
  void invalidate() { _dirty = true; }

  Services& services() { return _svc; }
  uint32_t now() { return _svc.sys->millis(); }

  void drawAll();

  // Banner over the top of the content area, for `ms` milliseconds.
  void notify(const char* title, const char* text, uint32_t ms = 2500);
  bool bannerVisible() { return _bannerUntil && (int32_t)(now() - _bannerUntil) < 0; }
  // Mesh events only, no drawing (used while the screen is off). Returns true if any.
  bool pollEvents();

private:
  void drawStatusBar();
  void drawHelpLine();
  void drawBanner();

  Services _svc;
  lgfx::LovyanGFX& _g;
  Screen* _stack[MAX_DEPTH] = {};
  int _depth = 0;
  bool _dirty = true;
  uint32_t _lastStatusDraw = 0;
  uint32_t _bannerUntil = 0;
  char _bannerTitle[32] = {};
  char _bannerText[64] = {};
};
