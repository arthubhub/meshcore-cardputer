#include "TrafficTracer.h"
#include "../Services.h"
#include <string.h>

void TrafficTracer::clear() {
  for (Node& n : _nodes) n = Node();
  for (Link& l : _links) l = Link();
  memset(_packetMinutes, 0, sizeof(_packetMinutes));
  _me = -1;
  _packets = 0;
  _version++;
}

int TrafficTracer::sum(const uint8_t* minutes) {
  int s = 0;
  for (int i = 0; i < WINDOW_MIN; i++) s += minutes[i];
  return s;
}

void TrafficTracer::bump(uint8_t* minutes) {
  uint8_t& m = minutes[_minute % WINDOW_MIN];
  if (m < 255) m++;
}

int TrafficTracer::nodes() const {
  int n = 0;
  for (const Node& x : _nodes) n += x.used;
  return n;
}

int TrafficTracer::links() const {
  int n = 0;
  for (const Link& x : _links) n += x.used;
  return n;
}

int TrafficTracer::usage(int i) const { return i >= 0 && i < MAX_NODES && _nodes[i].used ? sum(_nodes[i].minutes) : 0; }
int TrafficTracer::linkUsage(int i) const { return i >= 0 && i < MAX_LINKS && _links[i].used ? sum(_links[i].minutes) : 0; }

void TrafficTracer::advance(uint32_t nowMs) {
  const uint32_t minute = nowMs / 60000;
  if (!_started) {
    _started = true;
    _minute = minute;
    return;
  }
  if (minute <= _minute) return;
  const uint32_t steps = minute - _minute;
  bool removed = false;
  for (uint32_t k = 0; k < steps && k < (uint32_t)WINDOW_MIN; k++) {   // the buckets entering the window start empty
    const int slot = (int)((_minute + 1 + k) % WINDOW_MIN);
    for (Node& n : _nodes) n.minutes[slot] = 0;
    for (Link& l : _links) l.minutes[slot] = 0;
    _packetMinutes[slot] = 0;
  }
  _minute = minute;
  // Forget what was not used in the whole window
  for (int i = 0; i < MAX_NODES; i++) {
    Node& n = _nodes[i];
    if (!n.used || i == _me || sum(n.minutes)) continue;
    n = Node();
    removed = true;
  }
  for (Link& l : _links) {
    if (l.used && (!sum(l.minutes) || !_nodes[l.a].used || !_nodes[l.b].used)) {
      l = Link();
      removed = true;
    }
  }
  if (_me >= 0 && nodes() == 1) {             // nobody left: a blank view again
    _nodes[_me] = Node();
    _me = -1;
    removed = true;
  }
  _packets = sum(_packetMinutes);
  if (removed) _version++;
}

int TrafficTracer::weakestNode() const {
  int w = -1;
  for (int i = 0; i < MAX_NODES; i++) {
    if (!_nodes[i].used || i == _me) continue;
    if (w < 0 || usage(i) < usage(w) || (usage(i) == usage(w) && (int32_t)(_nodes[i].lastMs - _nodes[w].lastMs) < 0)) w = i;
  }
  return w;
}

int TrafficTracer::findOrAdd(const uint8_t* id, int bytes, Kind kind, uint32_t nowMs) {
  if (kind == Kind::Me) {
    if (_me >= 0) return _me;
  } else {
    // The same node: equal IDs, or a shorter one that is the start of a longer one (packets of 1- and 2-byte paths)
    int found = -1;
    for (int i = 0; i < MAX_NODES; i++) {
      const Node& n = _nodes[i];
      if (!n.used || n.kind == Kind::Me) continue;
      const int common = n.idBytes < bytes ? n.idBytes : bytes;
      if (memcmp(n.id, id, (size_t)common) != 0) continue;
      if (found < 0 || n.idBytes == bytes) found = i;   // an exact length wins over a prefix
    }
    if (found >= 0) {
      Node& n = _nodes[found];
      if (bytes > n.idBytes) {                // learnt more of its key
        memcpy(n.id, id, (size_t)bytes);
        n.idBytes = (uint8_t)bytes;
        n.lookup = Lookup::Pending;
      }
      if (kind == Kind::Router) n.kind = Kind::Router;   // relays: a repeater, whatever else it sent
      return found;
    }
  }
  int slot = -1;
  for (int i = 0; i < MAX_NODES && slot < 0; i++) {
    if (!_nodes[i].used) slot = i;
  }
  if (slot < 0) {                             // full: the least used leaves, with its links
    slot = weakestNode();
    if (slot < 0) return -1;
    for (Link& l : _links) {
      if (l.used && (l.a == slot || l.b == slot)) l = Link();
    }
  }
  Node& n = _nodes[slot];
  n = Node();
  n.used = true;
  n.kind = kind;
  if (bytes > 3) bytes = 3;
  if (bytes > 0) memcpy(n.id, id, (size_t)bytes);
  n.idBytes = (uint8_t)bytes;
  n.order = _order++;
  n.lastMs = nowMs;
  if (kind == Kind::Me) {
    _me = slot;
    n.hopsFromMe = 0;
    n.lookup = Lookup::Known;
  }
  return slot;
}

int TrafficTracer::linkOf(int a, int b) {
  int free = -1, weakest = -1;
  for (int i = 0; i < MAX_LINKS; i++) {
    Link& l = _links[i];
    if (!l.used) {
      if (free < 0) free = i;
      continue;
    }
    if ((l.a == a && l.b == b) || (l.a == b && l.b == a)) return i;
    if (weakest < 0 || linkUsage(i) < linkUsage(weakest)) weakest = i;
  }
  const int slot = free >= 0 ? free : weakest;
  if (slot < 0) return -1;
  Link& l = _links[slot];
  l = Link();
  l.used = true;
  l.a = (uint8_t)a;
  l.b = (uint8_t)b;
  return slot;
}

void TrafficTracer::onPacket(const PacketLogFrame& f, const PacketRecord& r) {
  if (r.tx()) return;                         // what we send has not travelled yet
  advance(f.ms);
  // A flood's path is the route it took; a direct packet spends its path on the way: only its sender is known
  const bool flood = r.flood();
  const int hops = flood ? (f.pathLen & 63) : 0;
  const int bytes = (f.pathLen >> 6) + 1;
  int chain[66];
  int n = 0;
  if (r.source == PacketRecord::SRC_NODE && r.fromLen) {
    const int s = findOrAdd(r.from, r.fromLen, Kind::Client, f.ms);
    if (s >= 0) chain[n++] = s;
  }
  for (int i = 0; i < hops && f.path && n < 64; i++) {
    const int x = findOrAdd(f.path + i * bytes, bytes, Kind::Router, f.ms);
    if (x >= 0) chain[n++] = x;
  }
  if (!n) return;                             // nothing to trace (a channel message heard directly)
  const int me = findOrAdd(nullptr, 0, Kind::Me, f.ms);
  if (me < 0) return;
  chain[n++] = me;
  for (int i = 0; i < n; i++) {
    Node& x = _nodes[chain[i]];
    bump(x.minutes);
    x.lastMs = f.ms;
    const int fromMe = n - 1 - i;             // the last repeater is 1 hop from me, the sender the farthest
    if (fromMe < x.hopsFromMe) x.hopsFromMe = (uint8_t)fromMe;
    if (i + 1 < n && chain[i] != chain[i + 1]) {
      const int l = linkOf(chain[i], chain[i + 1]);
      if (l >= 0) bump(_links[l].minutes);
    }
  }
  bump(_packetMinutes);
  _packets = sum(_packetMinutes);
  _version++;
}

void TrafficTracer::resolve(int i, const NodeMatch* m, int count) {
  if (i < 0 || i >= MAX_NODES || !_nodes[i].used) return;
  Node& n = _nodes[i];
  if (count <= 0 || !m) {
    n.lookup = Lookup::Unknown;
    return;
  }
  memcpy(n.name, m[0].name, sizeof(n.name));
  n.name[sizeof(n.name) - 1] = 0;
  n.lookup = count == 1 ? Lookup::Known : Lookup::Ambiguous;
  n.hasPos = count == 1 && m[0].hasPos;      // several nodes share this ID: placing it could be wrong
  n.latE6 = m[0].latE6;
  n.lonE6 = m[0].lonE6;
  _version++;
}

int TrafficTracer::nextPending() const {
  for (int i = 0; i < MAX_NODES; i++) {
    if (_nodes[i].used && _nodes[i].kind != Kind::Me && _nodes[i].lookup == Lookup::Pending) return i;
  }
  return -1;
}

uint16_t TrafficTracer::heatColor(int usage) {
  float t = (float)usage / (float)BUSY;
  if (t > 1) t = 1;
  if (t < 0) t = 0;
  // Green -> yellow -> red: bright all the way (a straight green-to-red mix is a dull olive in the middle)
  const int r = t < 0.5f ? (int)(510 * t) : 255;
  const int g = t < 0.5f ? 255 : (int)(510 * (1 - t));
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3));
}
