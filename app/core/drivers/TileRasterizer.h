#pragma once
// Reads one tile from the SD card row by row, at the scale the view needs, into a sink (MAP-6). No tile is ever held in
// RAM: the two line buffers below are all it uses. Reading goes through TileSource, so compressed tiles (MAP-4) only
// change the source.
#include "../Services.h"
#include "TileGrid.h"

class RowSink {
public:
  virtual ~RowSink() {}
  // `w` big-endian RGB565 pixels, as stored in the tiles. Offsets are relative to the screen.
  virtual void row(int x, int y, int w, const uint16_t* pixels) = 0;
};

class TileRasterizer {
public:
  // False if the tile is missing or a read failed. Rows sent before a failure stay sent: the caller paints over them.
  bool draw(TileSource& source, const TileBlit& blit, RowSink& sink);

private:
  bool readRow(TileSource& source, const TileBlit& b, int sourceRow);
  uint16_t _source[TileGrid::TILE_SIZE];   // one row of the tile
  uint16_t _line[TileGrid::TILE_SIZE];     // the same row at the scale of the screen
};
