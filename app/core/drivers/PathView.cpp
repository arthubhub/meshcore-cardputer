#include "PathView.h"
#include <stdio.h>
#include <string.h>

int PathView::heldHops() const {
  if (!validSize()) return 0;
  const int fit = (int)(_cap / (size_t)hashBytes());
  return hops() < fit ? hops() : fit;
}

void PathView::format(char* out, size_t n) const {
  if (!n) return;
  out[0] = 0;
  const int held = heldHops();
  size_t o = 0;
  for (int h = 0; h < held; h++) {
    if (o + (size_t)hashBytes() * 2 + 4 >= n) {   // entry + '>' + ".." + end of string
      memcpy(out + o, "..", 3);
      return;
    }
    if (h) out[o++] = '>';
    for (int b = 0; b < hashBytes(); b++) o += (size_t)snprintf(out + o, n - o, "%02x", hash(h)[b]);
  }
  if (truncated() && o + 3 <= n) memcpy(out + o, "..", 3);
}

void MsgRoute::set(MsgPathKind k, uint8_t pathWire, const uint8_t* path) {
  *this = MsgRoute();
  PathView from(pathWire, path, 64);   // MeshCore's MAX_PATH_SIZE: the source holds the whole path
  if (!from.validSize() || (from.hops() && !path)) return;
  kind = k;
  wire = pathWire;
  const size_t held = (size_t)view().heldHops() * (size_t)view().hashBytes();
  if (held) memcpy(bytes, path, held);
}

void MsgRoute::setReversed(MsgPathKind k, uint8_t pathWire, const uint8_t* path) {
  *this = MsgRoute();
  PathView from(pathWire, path, 64);   // MeshCore's MAX_PATH_SIZE: the source holds the whole path
  if (!from.validSize() || (from.hops() && !path)) return;
  kind = k;
  wire = pathWire;
  const int size = from.hashBytes(), held = view().heldHops();
  for (int i = 0; i < held; i++) memcpy(bytes + i * size, from.hash(from.heldHops() - 1 - i), (size_t)size);
}
