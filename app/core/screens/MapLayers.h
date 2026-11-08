#pragma once
// What the map draws over its tiles (MAP-6). Each kind of content is one MapLayer: contacts, own position, the route of
// a message. Future tracks (MAP-1), coverage dots (RADIO-3) or signal dots (RADIO-2) are new layers, MapScreen does not
// change.
#include "../App.h"
#include "../drivers/MapProjection.h"

// The map view for a layer: positions on one side, screen pixels on the other.
class MapCanvas {
public:
  MapCanvas(App& app, lgfx::LovyanGFX& g, const MapProjection& view, int originY) : _app(app), _g(g), _view(view), _originY(originY) {}

  App& app() { return _app; }
  lgfx::LovyanGFX& gfx() { return _g; }
  const MapProjection& view() const { return _view; }

  // Screen pixel of a position. False when it is more than `margin` pixels outside the view.
  bool place(double lat, double lon, int margin, int& x, int& y) const;
  // A straight line between two positions, 2 pixels wide, safe at any distance.
  void line(double lat0, double lon0, double lat1, double lon1, uint16_t color, bool dashed);

private:
  App& _app;
  lgfx::LovyanGFX& _g;
  MapProjection _view;
  int _originY;
};

class MapLayer {
public:
  virtual ~MapLayer() {}
  virtual void draw(MapCanvas& canvas) = 0;
  // Extends `out` with what this layer shows, for framing. False: nothing to frame.
  virtual bool bounds(geo::Bounds& out) const { return false; }
  // Shown as an overlay, a layer can add keys to the map and say so on the help line (nullptr: the map's own).
  virtual bool onKey(App& app, const KeyEvent& e) { return false; }
  virtual const char* help() const { return nullptr; }
};

class ContactsLayer : public MapLayer {
public:
  void draw(MapCanvas& canvas) override;
};

class OwnPositionLayer : public MapLayer {
public:
  void draw(MapCanvas& canvas) override;
};
