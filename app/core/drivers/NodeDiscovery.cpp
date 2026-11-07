#include "NodeDiscovery.h"
#include <string.h>

namespace {
constexpr uint8_t REQ = 0x80;           // CTL_TYPE_NODE_DISCOVER_REQ
constexpr uint8_t RESP = 0x90;          // CTL_TYPE_NODE_DISCOVER_RESP, low 4 bits = node type
constexpr uint8_t PREFIX_ONLY = 0x01;   // answer with 8 bytes of public key: shorter on air
constexpr uint8_t TYPE_REPEATER = 2;    // ADV_TYPE_REPEATER
}  // namespace

bool NodeDiscovery::canStart(uint32_t nowMs) const {
  return !_started || nowMs - _startMs >= MIN_INTERVAL_MS;
}

void NodeDiscovery::start(uint32_t tag, uint32_t nowMs) {
  _started = true;
  _tag = tag;
  _startMs = nowMs;
}

bool NodeDiscovery::searching(uint32_t nowMs) const {
  return _started && nowMs - _startMs < WINDOW_MS;
}

uint32_t NodeDiscovery::secondsLeft(uint32_t nowMs) const {
  return searching(nowMs) ? (WINDOW_MS - (nowMs - _startMs) + 999) / 1000 : 0;
}

uint32_t NodeDiscovery::secondsUntilNext(uint32_t nowMs) const {
  return canStart(nowMs) ? 0 : (MIN_INTERVAL_MS - (nowMs - _startMs) + 999) / 1000;
}

void NodeDiscovery::buildRequest(uint8_t* out) const {
  out[0] = REQ | PREFIX_ONLY;
  out[1] = 1 << TYPE_REPEATER;   // filter: repeaters
  memcpy(&out[2], &_tag, 4);
}

bool NodeDiscovery::accept(const uint8_t* payload, size_t len, float snrHere, float rssi, uint32_t nowMs) {
  if (!_started || len < MIN_RESPONSE_LEN) return false;
  if ((payload[0] & 0xF0) != RESP || (payload[0] & 0x0F) != TYPE_REPEATER) return false;
  if (memcmp(&payload[2], &_tag, 4) != 0) return false;   // answer to another search

  DiscoveredNode n;
  memcpy(n.prefix, &payload[6], sizeof(n.prefix));
  n.type = payload[0] & 0x0F;
  n.snrThere = (int8_t)payload[1] / 4.0f;
  n.snrHere = snrHere;
  n.rssi = rssi;
  n.heardMs = nowMs;

  for (int i = 0; i < _count; i++) {
    if (memcmp(_nodes[i].prefix, n.prefix, sizeof(n.prefix)) == 0) {
      _nodes[i] = n;
      sort();
      return true;
    }
  }
  if (_count < MAX_NODES) _nodes[_count++] = n;
  else if (n.snrHere > _nodes[MAX_NODES - 1].snrHere) _nodes[MAX_NODES - 1] = n;   // sorted: last is the weakest
  sort();
  return true;
}

void NodeDiscovery::sort() {
  for (int i = 1; i < _count; i++) {
    DiscoveredNode v = _nodes[i];
    int j = i - 1;
    while (j >= 0 && _nodes[j].snrHere < v.snrHere) { _nodes[j + 1] = _nodes[j]; j--; }
    _nodes[j + 1] = v;
  }
}
