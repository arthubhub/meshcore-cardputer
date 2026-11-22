#pragma once
// Live packet tracer (screen): the nodes of the packets heard in the last 20 minutes, on a blank map (by position) or
// as a flow graph (columns by hops, me on the right), coloured from green (little used) to red (busy). Model: core/drivers/TrafficTracer.
//   , /   select a node (busiest first)      V  map / graph       C  start again       `  back
// Names and positions are looked up while the screen is open, two nodes per frame (an archive lookup reads the flash).
#include "../App.h"
#include "../drivers/TrafficTracer.h"

class TracerScreen : public Screen {
public:
  enum class View : uint8_t { Auto, Map, Graph };

  const char* title() const override { return tr("Packet tracer", "Traceur radio"); }
  const char* help() const override { return tr(",/ node  V map/graph  C clear  ` back", ",/ point  V carte/graphe  C effacer"); }
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

  static constexpr int LOOKUPS_PER_TICK = 2;

private:
  bool mapView(App& app) const;              // the view drawn: Auto picks the map when 2 nodes or more are placed
  void layoutGraph(const TrafficTracer& t);
  void layoutMap(App& app, const TrafficTracer& t);
  void sortByUse(const TrafficTracer& t);

  View _view = View::Auto;
  int _sel = -1;                             // node index, -1: none
  uint32_t _seenVersion = 0;
  uint32_t _lastRedraw = 0;
  int16_t _x[TrafficTracer::MAX_NODES], _y[TrafficTracer::MAX_NODES];
  uint8_t _byUse[TrafficTracer::MAX_NODES];
  int _n = 0;                                // nodes in _byUse
  float _kmPerPx = 0;                        // map view scale, 0 unknown
};
