#include "SdTiles.h"
#include <CpadvLog.h>

#if CPADV_MAP
#include <Arduino.h>
#include <SD.h>
#include "SdCard.h"

namespace {
File g_tile;
using BusLock = sdcard::Lock;
}  // namespace

bool SdTiles::begin() {
  if (!sdcard::begin()) return false;
  BusLock lock;
  File f = SD.open("/tiles/info.txt", FILE_READ);
  if (!f) {
    CPLOG_I("SD", "no /tiles/info.txt (copy the tiles folder made by tools/tiles/make_tiles.py)");
    return false;
  }
  char buf[64] = {};
  f.read((uint8_t*)buf, sizeof(buf) - 1);
  f.close();
  _ok = sscanf(buf, "%lf %lf %d %d", &_lat, &_lon, &_zmin, &_zmax) == 4;
  CPLOG_I("SD", "map tiles zoom %d-%d around %.4f, %.4f (%s)", _zmin, _zmax, _lat, _lon,
                _ok ? "ok" : "info.txt invalid");
  return _ok;
}

bool SdTiles::beginTile(int z, int x, int y) {
  if (!_ok) return false;
  char path[40];
  snprintf(path, sizeof(path), "/tiles/%d/%d/%d.bin", z, x, y);
  BusLock lock;
  g_tile = SD.open(path, FILE_READ);
  return (bool)g_tile;
}

bool SdTiles::readRow(int row, int x0, int w, uint16_t* dst) {
  BusLock lock;
  if (!g_tile || !g_tile.seek((row * 256 + x0) * 2)) return false;
  return g_tile.read((uint8_t*)dst, w * 2) == (size_t)(w * 2);
}

void SdTiles::endTile() {
  BusLock lock;
  if (g_tile) g_tile.close();
}
#endif
