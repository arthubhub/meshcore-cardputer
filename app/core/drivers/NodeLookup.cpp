#include "NodeLookup.h"
#include <stdio.h>
#include <string.h>

int NodeLookup::find(const uint8_t* hash, int bytes, Kind kind, NodeMatch* out, int max) {
  int found = 0;
  const int n = _nodes.contactCount();
  ContactView c;
  for (int i = 0; i < n; i++) {
    if (!_nodes.contactAt(i, c) || memcmp(c.pubKey, hash, (size_t)bytes) != 0) continue;
    if (kind == Kind::Relay && c.type != CONTACT_REPEATER && c.type != CONTACT_ROOM) continue;
    if (found < max) {
      NodeMatch& m = out[found];
      m.key = c.key;
      utf8Copy(m.name, sizeof(m.name), c.name);
      m.type = c.type;
      m.hasPos = c.hasPos;
      m.latE6 = c.latE6;
      m.lonE6 = c.lonE6;
    }
    found++;
  }
  return found;
}

bool NodeLookup::uniqueName(const uint8_t* hash, int bytes, Kind kind, char* out, size_t n) {
  NodeMatch m;
  if (find(hash, bytes, kind, &m, 1) != 1) return false;
  snprintf(out, n, "%s", m.name);
  return true;
}
