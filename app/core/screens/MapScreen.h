#pragma once
#include "../App.h"
#include "../Widgets.h"

// Offline map: raster tiles read row by row from the SD card (480-byte line buffer, no tile
// cache in RAM), own position and contacts as markers, scale bar.
class MapScreen : public Screen {
public:
  const char* title() const override { return "Carte"; }
  const char* help() const override { return "+- zoom  ;.,/ déplacer  0 me centrer"; }
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

  // Last render duration, for the logs / system screen.
  uint32_t lastRenderMs() const { return _renderMs; }

private:
  void centerOn(double lat, double lon);
  void setZoom(App& app, int z);
  bool ownPixel(App& app, double& px, double& py);
  void drawTiles(App& app, lgfx::LovyanGFX& g, double left, double top, int& missing);
  void drawMarkers(App& app, lgfx::LovyanGFX& g, double left, double top);
  void drawScale(App& app, lgfx::LovyanGFX& g);

  int _z = 13;
  double _cx = 0, _cy = 0;        // map centre, global pixels at zoom _z
  bool _hasCenter = false;
  bool _follow = true;            // keep centred on the GPS position
  double _lastOwnX = -1, _lastOwnY = -1;
  uint32_t _lastCheck = 0;
  uint32_t _renderMs = 0;
};
