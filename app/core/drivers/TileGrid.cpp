#include "TileGrid.h"
#include <math.h>

static int floorDiv(int a, int b) {
  int q = a / b;
  return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

void TileGrid::setLevels(int zmin, int zmax) {
  _hasLevels = zmin <= zmax;
  _zmin = zmin;
  _zmax = zmax;
}

int TileGrid::clampZoom(int z) const {
  if (z < MIN_ZOOM) return MIN_ZOOM;
  int top = maxZoom();
  return z > top ? top : z;
}

bool TileGrid::hasDataAt(int z) const {
  return _hasLevels && z >= _zmin - MAX_REDUCE && z <= _zmax + MAX_ENLARGE;
}

int TileGrid::shiftAt(int z) const {
  if (!hasDataAt(z)) return 0;
  return z > _zmax ? z - _zmax : z < _zmin ? z - _zmin : 0;
}

void TileGrid::begin(double left, double top, int z) {
  _z = z;
  _data = hasDataAt(z);
  _shift = shiftAt(z);
  _dataZ = z - _shift;
  _cell = _shift >= 0 ? TILE_SIZE << _shift : TILE_SIZE >> -_shift;
  _x0 = (int)floor(left);
  _y0 = (int)floor(top);
  _tx0 = floorDiv(_x0, _cell);
  _tx1 = floorDiv(_x0 + _w - 1, _cell);
  _ty = floorDiv(_y0, _cell);
  _ty1 = floorDiv(_y0 + _h - 1, _cell);
  _tx = _tx0;
}

bool TileGrid::next(TileBlit& out) {
  if (_ty > _ty1) return false;
  const int sx = _tx * _cell - _x0, sy = _ty * _cell - _y0;   // corner of the scaled tile on the screen
  const int cx0 = sx < 0 ? -sx : 0, cy0 = sy < 0 ? -sy : 0;
  const int cx1 = sx + _cell > _w ? _w - sx : _cell, cy1 = sy + _cell > _h ? _h - sy : _cell;
  const int tiles = 1 << _dataZ;
  out = TileBlit();
  out.hasData = _data && _ty >= 0 && _ty < tiles;
  out.z = _dataZ;
  out.x = ((_tx % tiles) + tiles) % tiles;   // the world wraps horizontally
  out.y = _ty;
  out.shift = _shift;
  out.screenX = sx + cx0;
  out.screenY = sy + cy0;
  out.w = cx1 - cx0;
  out.h = cy1 - cy0;
  out.localX = cx0;
  out.localY = cy0;
  if (++_tx > _tx1) {
    _tx = _tx0;
    _ty++;
  }
  return true;
}
