#pragma once
// Turns a route into points of the map (MESSAGING-10). A path only holds repeater IDs: their positions come from the
// contacts, which may not know a repeater, or know several with the same ID. Each point says how far it can be trusted.
#include "NodeLookup.h"

enum class HopState : uint8_t {
  Endpoint,    // the sender or the receiver
  Single,      // one known repeater has this ID (another unknown one could share it)
  Ambiguous,   // several have it: the one that fits the route best is shown
  Unknown,     // no known repeater has it
};

struct RoutePoint {
  HopState state = HopState::Unknown;
  ConvKey key;                 // to open its contact sheet, when known
  bool hasPos = false;
  int32_t latE6 = 0, lonE6 = 0;
  uint8_t hash[3] = {};
  uint8_t hashBytes = 0;
  uint8_t candidates = 0;      // repeaters with this ID
  char name[16] = {};
};

struct RouteEndpoint {
  const char* name = "";
  ConvKey key;
  bool hasPos = false;
  double lat = 0, lon = 0;
};

struct ResolvedRoute {
  static constexpr int MAX_POINTS = MSG_PATH_MAX + 2;
  RoutePoint points[MAX_POINTS];   // sender first
  int count = 0;
  MsgPathKind kind = PATH_NONE;
  int hops = 0;                    // hops of the route, including those not held
  int notHeld = 0;                 // hops cut from the record
  int placed = 0;                  // hops with a position
  int ambiguous = 0, unknown = 0;
};

class RouteResolver {
public:
  static constexpr int MAX_CANDIDATES = 8;

  explicit RouteResolver(NodeDirectory& nodes) : _lookup(nodes) {}
  void resolve(const MsgRoute& route, const RouteEndpoint& start, const RouteEndpoint& end, ResolvedRoute& out);

private:
  void identify(const uint8_t* hash, int bytes, RoutePoint& p);
  void settle(ResolvedRoute& r, int index);
  NodeLookup _lookup;
};
