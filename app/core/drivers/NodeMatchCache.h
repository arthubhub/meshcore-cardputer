#pragma once
// Recent answers of a node lookup by ID (MESSAGING-12): the packet log and the routes ask for the same repeaters many
// times per second while the screen is drawn, and an archive lookup reads a bucket (20 ms at 240 MHz, 60 ms at 80).
// Keeps SIZE answers of up to PER matches each for TTL_MS; the oldest goes first. RAM: about 2.5 KB.
#include <stdint.h>
#include "../Services.h"

class NodeMatchCache {
public:
  static constexpr int SIZE = 32;
  static constexpr int PER = 4;
  static constexpr uint32_t TTL_MS = 30000;

  // True when a fresh answer is known: `count` matches exist, up to `max` (and PER) copied into `out`. An answer that
  // held more than PER matches only serves calls that want at most PER.
  bool get(const uint8_t* hash, int bytes, bool relays, uint32_t nowMs, NodeMatch* out, int max, int& count);
  void put(const uint8_t* hash, int bytes, bool relays, uint32_t nowMs, const NodeMatch* m, int count);
  void clear();

private:
  struct Entry {
    uint8_t hash[3];
    uint8_t bytes = 0;            // 0: free
    bool relays = false;
    uint8_t held = 0;
    int16_t count = 0;
    uint32_t atMs = 0;
    NodeMatch m[PER];
  };
  Entry _e[SIZE];
};
