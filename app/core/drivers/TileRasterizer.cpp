#include "TileRasterizer.h"

// Tile pixel shown at position `local` of the scaled tile
static int sourcePos(int local, int shift) { return shift >= 0 ? local >> shift : local << -shift; }

bool TileRasterizer::readRow(TileSource& source, const TileBlit& b, int sourceRow) {
  const int first = sourcePos(b.localX, b.shift);
  const int last = sourcePos(b.localX + b.w - 1, b.shift);
  if (!source.readRow(sourceRow, first, last - first + 1, _source)) return false;
  for (int i = 0; i < b.w; i++) _line[i] = _source[sourcePos(b.localX + i, b.shift) - first];
  return true;
}

bool TileRasterizer::draw(TileSource& source, const TileBlit& b, RowSink& sink) {
  if (!b.hasData || !source.beginTile(b.z, b.x, b.y)) return false;
  bool ok = true;
  int loaded = -1;   // an enlarged row repeats: it is read once
  for (int i = 0; i < b.h && ok; i++) {
    const int row = sourcePos(b.localY + i, b.shift);
    if (row != loaded) {
      ok = readRow(source, b, row);
      loaded = row;
    }
    if (ok) sink.row(b.screenX, b.screenY + i, b.w, _line);
  }
  source.endTile();
  return ok;
}
