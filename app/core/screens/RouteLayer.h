#pragma once
#include "MapLayers.h"
#include "../drivers/RouteResolver.h"

// The way a message went, on the map (MESSAGING-10). A straight line between the points: where it really travelled
// between two repeaters is not known.
//  - plain: the route read from the packet, between points that follow each other;
//  - orange: one end is an ambiguous hop, the best guess among repeaters sharing an ID;
//  - dashed: a planned route, or a gap where a hop has no known position.
class RouteLayer : public MapLayer {
public:
  void show(const ResolvedRoute* route) { _route = route; }
  void draw(MapCanvas& canvas) override;
  bool bounds(geo::Bounds& out) const override;

private:
  const ResolvedRoute* _route = nullptr;
};
