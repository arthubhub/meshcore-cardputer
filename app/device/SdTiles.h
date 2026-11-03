#pragma once
// Offline map tiles on the SD card (optional feature: built only with CPADV_MAP=1).
// The SD card shares the SPI bus with the SX1262: every card access holds the bus lock
// (and the radio driver runs in the same task, so a radio transfer can never interleave).
#include "../core/Services.h"

#ifndef CPADV_MAP
  #define CPADV_MAP 0   // map off unless the _map env turns it on
#endif

#if CPADV_MAP
class SdTiles : public TileSource {
public:
  bool begin();   // mounts the card and reads /tiles/info.txt; false without card or tiles
  bool available() override { return _ok; }
  bool zoomRange(int& zmin, int& zmax) override { zmin = _zmin; zmax = _zmax; return _ok; }
  bool hint(double& lat, double& lon) override { lat = _lat; lon = _lon; return _ok; }
  bool beginTile(int z, int x, int y) override;
  bool readRow(int row, int x0, int w, uint16_t* dst) override;
  void endTile() override;

private:
  bool _ok = false;
  int _zmin = 0, _zmax = 0;
  double _lat = 0, _lon = 0;
};
#endif
