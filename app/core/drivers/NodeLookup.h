#pragma once
// Finds nodes from the few bytes of public key a packet carries (MESSAGING-10). One byte IDs collide: on a large mesh
// several repeaters share one, so a lookup returns every candidate, never a guess.
#include "../Services.h"

class NodeLookup {
public:
  explicit NodeLookup(NodeDirectory& nodes) : _nodes(nodes) {}

  enum class Kind : uint8_t { Any, Relay };   // Relay: the nodes that forward packets

  // Fills up to `max` matches and returns how many exist, which may be more.
  int find(const uint8_t* hash, int bytes, Kind kind, NodeMatch* out, int max);
  // The name, when exactly one node matches.
  bool uniqueName(const uint8_t* hash, int bytes, Kind kind, char* out, size_t n);

private:
  NodeDirectory& _nodes;
};
