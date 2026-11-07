#pragma once
#include "../App.h"
#include "../Widgets.h"
#include "RouteLayer.h"

class ContactDetailScreen;
class MapScreen;

// The hops of a route, from the sender to the receiver, each with what is known of it (MESSAGING-10). T shows it on the
// map, in firmwares that have one.
class RouteScreen : public Screen {
public:
  explicit RouteScreen(ContactDetailScreen& contact) : _contact(contact) {}
  void setMap(MapScreen* map) { _map = map; }

  // Prepares the screen; call before App::push().
  void show(App& app, const MsgRoute& route, const RouteEndpoint& start, const RouteEndpoint& end);
  const ResolvedRoute& route() const { return _route; }

  const char* title() const override { return _title; }
  const char* help() const override { return tr("Enter:contact  T:map  `:back", "Entrée:contact  T:carte  `:retour"); }
  void onEnter(App& app) override { _list = ListState(); }
  bool onKey(App& app, const KeyEvent& e) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  int rowCount() const { return _route.count + (_route.notHeld ? 1 : 0); }
  // The point shown on a row, nullptr on the line telling that hops were cut
  const RoutePoint* pointAt(int row, int* number) const;
  void openContact(App& app);
  void openMap(App& app);

  ContactDetailScreen& _contact;
  MapScreen* _map = nullptr;
  ResolvedRoute _route;
  RouteLayer _layer;
  ListState _list;
  char _title[32] = "Route";
};
