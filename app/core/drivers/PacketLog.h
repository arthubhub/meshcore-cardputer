#pragma once
// The last packets heard and sent (RADIO-6). Portable: it decodes the wire fields itself, so it is tested without
// MeshCore here, and against MeshCore's real packets in sim/wire. Fixed ring in RAM, nothing is ever allocated.
#include <stddef.h>
#include <stdint.h>
#include "PathView.h"

namespace wire {
// MeshCore packet header (meshcore/src/Packet.h). sim/wire checks these against MeshCore's own values.
enum Route : uint8_t { ROUTE_TRANSPORT_FLOOD = 0, ROUTE_FLOOD = 1, ROUTE_DIRECT = 2, ROUTE_TRANSPORT_DIRECT = 3 };
enum Payload : uint8_t {
  PL_REQ = 0x00, PL_RESPONSE = 0x01, PL_TXT = 0x02, PL_ACK = 0x03, PL_ADVERT = 0x04, PL_GRP_TXT = 0x05,
  PL_GRP_DATA = 0x06, PL_ANON_REQ = 0x07, PL_PATH = 0x08, PL_TRACE = 0x09, PL_MULTIPART = 0x0A, PL_CONTROL = 0x0B,
  PL_RAW = 0x0F,
};
constexpr uint8_t header(uint8_t route, uint8_t payload) { return (uint8_t)((payload << 2) | route); }
}  // namespace wire

struct PacketRecord {
  enum Flags : uint8_t { TX = 1, HAS_TO = 2, HAS_CODE = 4, OUR_REGION = 8 };
  enum Source : uint8_t { SRC_NONE, SRC_NODE, SRC_CHANNEL };

  uint32_t seq = 0;
  uint32_t ms = 0;            // uptime
  uint32_t unixTime = 0;      // our clock, 0 if unknown
  int16_t rssi = 0;
  uint16_t transportCode = 0;
  int8_t snrX4 = 0;
  uint8_t len = 0;            // bytes on air
  uint8_t type = 0;           // wire::Payload
  uint8_t route = 0;          // wire::Route
  uint8_t hops = 0;
  uint8_t hashBytes = 1;      // path ID size chosen by the original sender
  uint8_t flags = 0;
  uint8_t source = SRC_NONE;
  uint8_t fromLen = 0;        // bytes known in from[] (the identity of the sender, or the channel hash)
  uint8_t to = 0;             // destination hash, when flags has HAS_TO
  uint8_t from[3] = {};
  uint8_t lastHop[3] = {};    // the repeater we heard it from (hashBytes bytes), when hops > 0

  bool tx() const { return flags & TX; }
  bool flood() const { return route == wire::ROUTE_FLOOD || route == wire::ROUTE_TRANSPORT_FLOOD; }
  bool scoped() const { return flags & HAS_CODE; }
  bool ourRegion() const { return flags & OUR_REGION; }
};

class PacketLog {
public:
  static constexpr int CAPACITY = 48;
  static constexpr size_t CSV_LINE_MAX = 96;

  // One packet, as read from MeshCore's Packet object.
  struct Frame {
    uint8_t header = 0;
    uint8_t pathLen = 0;                // wire byte: size in the top 2 bits, count in the low 6
    const uint8_t* path = nullptr;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint16_t transportCode = 0;
    int rawLen = 0;
    float snr = 0;
    float rssi = 0;
    uint32_t ms = 0;
    uint32_t unixTime = 0;
    bool tx = false;
    bool ourRegion = false;
  };

  void add(const Frame& f);
  void clear();
  uint32_t total() const { return _total; }     // packets recorded since the last clear(), also the next seq
  int count() const { return _total < CAPACITY ? (int)_total : CAPACITY; }
  const PacketRecord* recent(int i) const;      // 0 = newest
  const PacketRecord* bySeq(uint32_t seq) const;   // nullptr: too old, already overwritten

  static const char* typeName(uint8_t type);    // "ADV", "TXT"...
  static const char* routeName(uint8_t route);  // "F", "D", "TF", "TD"
  static const char* csvHeader();
  static void formatCsv(const PacketRecord& r, char* out, size_t n);

private:
  static void decode(const Frame& f, PacketRecord& r);
  PacketRecord _r[CAPACITY];
  uint32_t _total = 0;
};
