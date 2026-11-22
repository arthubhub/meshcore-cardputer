#pragma once
// Live packet tracer: which repeaters and which senders the packets we hear went through, and how much in the last
// 20 minutes. Fed by the packet log (PacketObserver) with the whole path of every flood received; the screen
// (TracerScreen) draws it on a blank map or as a graph. Portable, fixed RAM (about 4 KB), unit-tested.
//
//  - A node is a repeater of a path (Router), the sender of a packet (Client: an advert's key, a direct message's
//    sender ID) or this Cardputer (Me). It is known by the 1 to 3 bytes of its key the packets carry; a shorter ID
//    that matches a longer one already known is the same node.
//  - A link is two nodes one hop apart on a path (sender -> first repeater -> ... -> last repeater -> me).
//  - Use: packets per minute over the last WINDOW_MIN minutes (a ring of one-minute buckets). Unused for that long, a
//    node or link leaves: the view starts blank and empties again on a quiet mesh.
//  - heatColor(): green (0,255,0) for a little-used node to red (255,0,0) at BUSY packets in the window, through
//    yellow.
// Names and positions are not known here: the screen looks them up (a few per frame, through NodeLookup) and keeps
// them with resolve().
#include <stdint.h>
#include "PacketLog.h"

struct NodeMatch;

class TrafficTracer : public PacketObserver {
public:
  static constexpr int MAX_NODES = 48;
  static constexpr int MAX_LINKS = 96;
  static constexpr int WINDOW_MIN = 20;
  static constexpr int BUSY = 30;             // packets in the window for full red

  enum class Kind : uint8_t { Me, Router, Client };
  enum class Lookup : uint8_t { Pending, Known, Ambiguous, Unknown };

  struct Node {
    uint8_t id[3] = {};
    uint8_t idBytes = 0;
    Kind kind = Kind::Router;
    uint8_t hopsFromMe = 0xFF;                // fewest hops seen between it and me
    uint8_t minutes[WINDOW_MIN] = {};
    uint32_t lastMs = 0;
    uint32_t order = 0;                       // when it appeared (stable layouts)
    Lookup lookup = Lookup::Pending;
    char name[16] = {};
    bool hasPos = false;
    int32_t latE6 = 0, lonE6 = 0;
    bool used = false;
  };
  struct Link {
    uint8_t a = 0, b = 0;                     // node indexes, a on the sender's side
    uint8_t minutes[WINDOW_MIN] = {};
    bool used = false;
  };

  void onPacket(const PacketLogFrame& f, const PacketRecord& r) override;
  // Moves the window to `nowMs` (packets older than WINDOW_MIN minutes are forgotten); the screen calls it too
  void advance(uint32_t nowMs);
  void clear();

  int nodeCapacity() const { return MAX_NODES; }
  const Node& node(int i) const { return _nodes[i]; }
  const Link& link(int i) const { return _links[i]; }
  int nodes() const;                          // in use
  int links() const;
  int usage(int node) const;                  // packets in the window
  int linkUsage(int link) const;
  int packets() const { return _packets; }    // floods traced in the window (approximate: per minute buckets)
  uint32_t version() const { return _version; }   // changes with every traced packet or forgotten node
  int me() const { return _me; }              // -1 before the first packet

  // The screen's lookup of a node: matches from NodeLookup (count = how many nodes have that ID)
  void resolve(int node, const NodeMatch* m, int count);
  int nextPending() const;                    // a node still to look up, -1 if none

  static uint16_t heatColor(int usage);       // RGB565

private:
  int findOrAdd(const uint8_t* id, int bytes, Kind kind, uint32_t nowMs);
  int linkOf(int a, int b);
  static int sum(const uint8_t* minutes);
  void bump(uint8_t* minutes);
  int weakestNode() const;

  Node _nodes[MAX_NODES];
  Link _links[MAX_LINKS];
  int _me = -1;
  uint32_t _minute = 0;                       // the current minute (since boot)
  bool _started = false;
  uint32_t _version = 0, _order = 0;
  int _packets = 0;
  uint8_t _packetMinutes[WINDOW_MIN] = {};
};
