#pragma once
#include "../App.h"
#include "../Widgets.h"
#include "../drivers/TileGrid.h"
#include "../drivers/TileRasterizer.h"
#include "MapLayers.h"

// Offline map: raster tiles read row by row from the SD card (no tile in RAM), then its layers, then the scale bar.
// showOverlay() opens the map framed on one more layer, and gives the previous view back when the user leaves.
class MapScreen : public Screen {
public:
  MapScreen();

  // Call before App::push(). The layer must outlive the screen; it is forgotten when the user leaves.
  void showOverlay(MapLayer* layer) { _overlay = layer; _frameOverlay = true; }

  const char* title() const override { return tr("Map", "Carte"); }
  const char* help() const override;
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

  int zoom() const { return _view.z; }
  // Last render duration, for the logs / system screen.
  uint32_t lastRenderMs() const { return _renderMs; }

private:
  struct View {
    int z = 13;
    double cx = 0, cy = 0;        // map centre, global pixels at zoom z
    bool follow = true;           // keep centred on the GPS position
  };

  void syncLevels(App& app);
  void centerOn(double lat, double lon);
  void setZoom(App& app, int z);
  void frameOverlay(App& app);
  void leaveOverlay();
  bool ownPixel(App& app, double& px, double& py);
  void drawTiles(App& app, lgfx::LovyanGFX& g, double left, double top, int& missing);
  void drawScale(App& app, lgfx::LovyanGFX& g);

  TileGrid _grid;
  TileRasterizer _raster;
  ContactsLayer _contacts;
  OwnPositionLayer _own;
  MapLayer* _overlay = nullptr;
  bool _frameOverlay = false;

  View _view;
  View _beforeOverlay;
  bool _hadCenterBeforeOverlay = false;
  bool _hasCenter = false;
  double _lastOwnX = -1, _lastOwnY = -1;
  uint32_t _lastCheck = 0;
  uint32_t _renderMs = 0;
};
