#pragma once
#include "../App.h"
#include "../Widgets.h"
#include "MessagingScreens.h"
#include "../drivers/HeadingFilter.h"

// Compass towards a contact: arrow, distance, bearing, age of the contact's position.
// No magnetometer on the Cardputer: the arrow is relative to the direction of travel when
// moving (GPS course, through HeadingFilter), otherwise north is up.
class CompassScreen : public Screen {
public:
  void setTarget(const ConvKey& key) { _target = key; }

  const char* title() const override { return tr("Compass", "Boussole"); }
  const char* help() const override { return tr(",/ contact  M write  ` back", ",/ contact  M écrire  ` retour"); }
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

  void setChat(ChatScreen* chat) { _chat = chat; }
  bool headingUp() const { return _heading.headingUp(); }

private:
  bool pickTarget(App& app, int step, ContactView& out);   // step 0: current (or first)
  HeadingFilter _heading;
  ConvKey _target;
  ChatScreen* _chat = nullptr;
  uint32_t _lastRefresh = 0;
};

class GpsScreen : public Screen {
public:
  explicit GpsScreen(CompassScreen& compass) : _compass(compass) {}
  const char* title() const override { return "GPS"; }
  const char* help() const override { return tr("A:advert+pos B:compass R:restart", "A:annonce+pos B:boussole R:redém."); }
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  CompassScreen& _compass;
  ConfirmPress _confirm;
  uint32_t _lastRefresh = 0;
};
