#include "RouteResolver.h"
#include "../Geo.h"
#include <string.h>

namespace {

void setEndpoint(RoutePoint& p, const RouteEndpoint& e) {
  p = RoutePoint();
  p.state = HopState::Endpoint;
  p.key = e.key;
  p.hasPos = e.hasPos;
  p.latE6 = (int32_t)(e.lat * 1e6);
  p.lonE6 = (int32_t)(e.lon * 1e6);
  utf8Copy(p.name, sizeof(p.name), e.name);
}

double distance(const RoutePoint& a, int32_t latE6, int32_t lonE6) {
  return geo::distanceM(a.latE6 / 1e6, a.lonE6 / 1e6, latE6 / 1e6, lonE6 / 1e6);
}

void place(RoutePoint& p, const NodeMatch& m) {
  p.key = m.key;
  p.hasPos = m.hasPos;
  p.latE6 = m.latE6;
  p.lonE6 = m.lonE6;
  utf8Copy(p.name, sizeof(p.name), m.name);
}

}  // namespace

void RouteResolver::identify(const uint8_t* hash, int bytes, RoutePoint& p) {
  p = RoutePoint();
  memcpy(p.hash, hash, (size_t)bytes);
  p.hashBytes = (uint8_t)bytes;
  NodeMatch m[1];
  const int found = _lookup.find(hash, bytes, NodeLookup::Kind::Relay, m, 1);
  p.candidates = (uint8_t)(found > 255 ? 255 : found);
  if (found == 0) {
    p.state = HopState::Unknown;
  } else if (found == 1) {
    p.state = HopState::Single;
    place(p, m[0]);
  } else {
    p.state = HopState::Ambiguous;   // settled once the sure points around it are known
  }
}

// Picks the candidate that lengthens the route the least between the nearest placed points on each side. Going from
// the sender, an ambiguous hop already settled counts as placed for the next one.
void RouteResolver::settle(ResolvedRoute& r, int index) {
  RoutePoint& p = r.points[index];
  const RoutePoint* before = nullptr;
  const RoutePoint* after = nullptr;
  for (int i = index - 1; i >= 0 && !before; i--) if (r.points[i].hasPos) before = &r.points[i];
  for (int i = index + 1; i < r.count && !after; i++) if (r.points[i].hasPos) after = &r.points[i];

  NodeMatch all[MAX_CANDIDATES];
  const int found = _lookup.find(p.hash, p.hashBytes, NodeLookup::Kind::Relay, all, MAX_CANDIDATES);
  const int n = found < MAX_CANDIDATES ? found : MAX_CANDIDATES;
  int best = -1;
  double bestCost = 0;
  for (int i = 0; i < n; i++) {
    if (!all[i].hasPos) continue;
    double cost = 0;
    if (before) cost += distance(*before, all[i].latE6, all[i].lonE6);
    if (after) cost += distance(*after, all[i].latE6, all[i].lonE6);
    if (best < 0 || cost < bestCost) { best = i; bestCost = cost; }
  }
  if (best >= 0) place(p, all[best]);
}

void RouteResolver::resolve(const MsgRoute& route, const RouteEndpoint& start, const RouteEndpoint& end, ResolvedRoute& out) {
  out = ResolvedRoute();
  const PathView path = route.view();
  const int held = route.known() ? path.heldHops() : 0;
  out.kind = (MsgPathKind)route.kind;
  out.hops = route.known() ? path.hops() : 0;
  out.notHeld = out.hops - held;

  setEndpoint(out.points[out.count++], start);
  for (int i = 0; i < held; i++) identify(path.hash(i), path.hashBytes(), out.points[out.count++]);
  setEndpoint(out.points[out.count++], end);

  for (int i = 1; i < out.count - 1; i++) {
    if (out.points[i].state == HopState::Ambiguous) settle(out, i);
  }
  for (int i = 1; i < out.count - 1; i++) {
    const RoutePoint& p = out.points[i];
    if (p.hasPos) out.placed++;
    if (p.state == HopState::Ambiguous) out.ambiguous++;
    if (p.state == HopState::Unknown) out.unknown++;
  }
}
