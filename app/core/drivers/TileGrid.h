#pragma once
// Which map tiles cover the screen at a zoom level (MAP-6). Pure geometry, no screen and no file.
//
// The SD card holds a range of levels. Near that range the map still draws, by enlarging or reducing the closest
// level. Further away there is nothing to read: the view shows the grid and the markers, which long routes need.
#include <stdint.h>

struct TileBlit {
  bool hasData = false;   // false: no tile here (outside the world, or no level to read at this zoom)
  int z = 0, x = 0, y = 0;   // the tile to read
  int shift = 0;          // tile pixels are drawn 2^shift times larger (negative: smaller)
  int screenX = 0, screenY = 0, w = 0, h = 0;   // the part of the screen it covers
  int localX = 0, localY = 0;                   // where that part starts inside the scaled tile
};

class TileGrid {
public:
  static constexpr int TILE_SIZE = 256;
  static constexpr int MIN_ZOOM = 3;
  static constexpr int NO_TILES_MAX_ZOOM = 17;
  static constexpr int MAX_ENLARGE = 2, MAX_REDUCE = 2;   // levels of scaling around the tiles on the card

  TileGrid(int screenW, int screenH) : _w(screenW), _h(screenH) {}

  void setNoLevels() { _hasLevels = false; }
  void setLevels(int zmin, int zmax);

  int maxZoom() const { return _hasLevels ? _zmax + MAX_ENLARGE : NO_TILES_MAX_ZOOM; }
  int clampZoom(int z) const;
  bool hasDataAt(int z) const;
  // Scale of the tiles at this zoom: 2 = shown 4 times larger, -1 = 2 times smaller. 0 without data.
  int shiftAt(int z) const;

  // Walks the tiles of a view: `left`, `top` are global pixels of the screen's corner at zoom z.
  void begin(double left, double top, int z);
  bool next(TileBlit& out);

private:
  int _w, _h;
  bool _hasLevels = false;
  int _zmin = 0, _zmax = 0;
  int _z = 0, _shift = 0, _dataZ = 0, _cell = TILE_SIZE;
  bool _data = false;
  int _x0 = 0, _y0 = 0;
  int _tx = 0, _ty = 0, _tx0 = 0, _tx1 = 0, _ty1 = 0;
};
