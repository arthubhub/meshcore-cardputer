#pragma once
// Search for the repeaters within direct radio range (RADIO-8), with MeshCore's zero-hop discovery packets
// (meshcore/examples/simple_repeater/MyMesh.cpp, onControlDataRecv). Portable: builds the request, checks the
// answers and keeps the table; the mesh sends and receives the bytes. Tested on the PC.
//
// Answers add up from one search to the next: on a weak link about one packet in two is lost, so a repeater
// missed by one search is often caught by the next.
#include <stddef.h>
#include <stdint.h>

struct DiscoveredNode {
  uint8_t prefix[8] = {};     // start of the public key
  uint8_t type = 0;           // ContactType
  float snrThere = 0;         // dB: how the repeater heard our request
  float snrHere = 0;          // dB: how we heard its answer
  float rssi = 0;             // dBm: its answer
  uint32_t heardMs = 0;
};

class NodeDiscovery {
public:
  static constexpr int MAX_NODES = 16;
  static constexpr size_t REQUEST_LEN = 6;
  static constexpr size_t MIN_RESPONSE_LEN = 14;      // header + 8 bytes of public key
  static constexpr uint32_t WINDOW_MS = 15000;        // answers come after a random delay
  static constexpr uint32_t MIN_INTERVAL_MS = 30000;  // a repeater answers 4 requests per 2 minutes

  bool canStart(uint32_t nowMs) const;
  void start(uint32_t tag, uint32_t nowMs);           // the caller checked canStart()
  bool searching(uint32_t nowMs) const;
  uint32_t secondsLeft(uint32_t nowMs) const;         // of the current search
  uint32_t secondsUntilNext(uint32_t nowMs) const;    // 0 = a search can start now

  // Writes the request of the current search into `out` (REQUEST_LEN bytes).
  void buildRequest(uint8_t* out) const;
  // Takes a control packet payload. True if it is an answer to the current search from a repeater.
  bool accept(const uint8_t* payload, size_t len, float snrHere, float rssi, uint32_t nowMs);

  int count() const { return _count; }
  const DiscoveredNode& at(int i) const { return _nodes[i]; }   // strongest answer first
  void clear() { _count = 0; }

private:
  void sort();
  DiscoveredNode _nodes[MAX_NODES];
  int _count = 0;
  bool _started = false;
  uint32_t _tag = 0;
  uint32_t _startMs = 0;
};
