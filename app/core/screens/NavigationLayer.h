#pragma once
#include "MapLayers.h"

class CompassScreen;

// The way to a contact, on the map (MAP-2): a line from my position to theirs, with the distance and the bearing.
// B switches to the compass towards them.
class NavigationLayer : public MapLayer {
public:
  void setCompass(CompassScreen* compass) { _compass = compass; }
  void setTarget(const ConvKey& key) { _target = key; }
  // Reads the two positions again. draw() does it; call it once before framing the map on this layer.
  void refresh(App& app);

  void draw(MapCanvas& canvas) override;
  bool bounds(geo::Bounds& out) const override;
  bool onKey(App& app, const KeyEvent& e) override;
  const char* help() const override;

private:
  bool target(App& app, ContactView& out) const;
  ConvKey _target;
  CompassScreen* _compass = nullptr;
  // bounds() has no App: the positions of the last draw (and of the opening) are kept
  bool _hasMe = false, _hasTarget = false;
  double _meLat = 0, _meLon = 0, _tLat = 0, _tLon = 0;
};
