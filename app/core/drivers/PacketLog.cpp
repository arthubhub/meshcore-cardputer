#include "PacketLog.h"
#include <stdio.h>
#include <string.h>

using namespace wire;

void PacketLog::decode(const Frame& f, PacketRecord& r) {
  r.route = f.header & 0x03;
  r.type = (f.header >> 2) & 0x0F;
  r.hops = f.pathLen & 63;
  r.hashBytes = (uint8_t)((f.pathLen >> 6) + 1);
  if (r.hashBytes > sizeof(r.lastHop)) r.hashBytes = 1;   // reserved encoding: MeshCore refuses such packets
  r.len = (uint8_t)(f.rawLen > 255 ? 255 : f.rawLen);
  r.flags = (f.tx ? PacketRecord::TX : 0);
  if (r.route == ROUTE_TRANSPORT_FLOOD || r.route == ROUTE_TRANSPORT_DIRECT) {
    r.flags |= PacketRecord::HAS_CODE;
    r.transportCode = f.transportCode;
    if (f.ourRegion) r.flags |= PacketRecord::OUR_REGION;
  }
  if (r.hops && f.path) memcpy(r.lastHop, f.path + (r.hops - 1) * r.hashBytes, r.hashBytes);

  const uint8_t* p = f.payload;
  const size_t n = p ? f.payloadLen : 0;
  const auto fromBytes = [&](size_t off, uint8_t source) {
    if (n <= off) return;
    size_t k = n - off < sizeof(r.from) ? n - off : sizeof(r.from);
    memcpy(r.from, p + off, k);
    r.fromLen = (uint8_t)k;
    r.source = source;
  };
  switch (r.type) {
    case PL_REQ: case PL_RESPONSE: case PL_TXT: case PL_PATH:   // dest hash, source hash, MAC...
      if (n >= 2) {
        r.to = p[0];
        r.flags |= PacketRecord::HAS_TO;
        r.from[0] = p[1];
        r.fromLen = 1;
        r.source = PacketRecord::SRC_NODE;
      }
      break;
    case PL_ANON_REQ:                                           // dest hash, sender's public key...
      if (n >= 1) { r.to = p[0]; r.flags |= PacketRecord::HAS_TO; }
      fromBytes(1, PacketRecord::SRC_NODE);
      break;
    case PL_ADVERT:                                             // public key first
      fromBytes(0, PacketRecord::SRC_NODE);
      break;
    case PL_GRP_TXT: case PL_GRP_DATA:                          // channel hash
      if (n >= 1) {
        r.from[0] = p[0];
        r.fromLen = 1;
        r.source = PacketRecord::SRC_CHANNEL;
      }
      break;
    case PL_CONTROL:                                            // discovery answer: type, SNR, tag, then the key
      if (n >= 7 && (p[0] & 0xF0) == 0x90) fromBytes(6, PacketRecord::SRC_NODE);
      break;
    default:
      break;   // ACK, TRACE, MULTIPART, RAW: no sender in clear
  }
}

void PacketLog::add(const Frame& f) {
  PacketRecord r;
  decode(f, r);
  r.seq = _total;
  r.ms = f.ms;
  r.unixTime = f.unixTime;
  r.rssi = (int16_t)f.rssi;
  float x4 = f.snr * 4.0f;
  r.snrX4 = (int8_t)(x4 > 127 ? 127 : x4 < -128 ? -128 : x4);
  _r[_total % CAPACITY] = r;
  _total++;
}

void PacketLog::clear() { _total = 0; }

const PacketRecord* PacketLog::recent(int i) const {
  if (i < 0 || i >= count()) return nullptr;
  return &_r[(_total - 1 - (uint32_t)i) % CAPACITY];
}

const PacketRecord* PacketLog::bySeq(uint32_t seq) const {
  if (seq >= _total || _total - seq > CAPACITY) return nullptr;
  return &_r[seq % CAPACITY];
}

const char* PacketLog::typeName(uint8_t t) {
  switch (t) {
    case PL_REQ: return "REQ";
    case PL_RESPONSE: return "RESP";
    case PL_TXT: return "TXT";
    case PL_ACK: return "ACK";
    case PL_ADVERT: return "ADV";
    case PL_GRP_TXT: return "GRP";
    case PL_GRP_DATA: return "GDAT";
    case PL_ANON_REQ: return "ANON";
    case PL_PATH: return "PATH";
    case PL_TRACE: return "TRACE";
    case PL_MULTIPART: return "MULTI";
    case PL_CONTROL: return "CTRL";
    case PL_RAW: return "RAW";
    default: return "?";
  }
}

const char* PacketLog::routeName(uint8_t route) {
  switch (route) {
    case ROUTE_FLOOD: return "F";
    case ROUTE_DIRECT: return "D";
    case ROUTE_TRANSPORT_FLOOD: return "TF";
    default: return "TD";
  }
}

const char* PacketLog::csvHeader() { return "seq,uptime_ms,unix,dir,type,route,hops,id_bytes,from,to,via,snr,rssi,len,region"; }

void PacketLog::formatCsv(const PacketRecord& r, char* out, size_t n) {
  char from[8] = "", to[4] = "", via[8] = "";
  size_t k = 0;
  for (int i = 0; i < r.fromLen; i++) k += snprintf(from + k, sizeof(from) - k, "%02x", r.from[i]);
  if (r.flags & PacketRecord::HAS_TO) snprintf(to, sizeof(to), "%02x", r.to);
  k = 0;
  if (r.hops) {
    for (int i = 0; i < r.hashBytes; i++) k += snprintf(via + k, sizeof(via) - k, "%02x", r.lastHop[i]);
  }
  snprintf(out, n, "%u,%u,%u,%s,%s,%s,%u,%u,%s,%s,%s,%.2f,%d,%u,%s", (unsigned)r.seq, (unsigned)r.ms, (unsigned)r.unixTime,
           r.tx() ? "tx" : "rx", typeName(r.type), routeName(r.route), r.hops, r.hashBytes, from, to, via, r.snrX4 / 4.0f,
           r.rssi, r.len, !r.scoped() ? "" : r.ourRegion() ? "ours" : "other");
}

void formatPathHashes(const uint8_t* path, size_t pathCap, int hops, int hashBytes, char* out, size_t n) {
  if (!n) return;
  out[0] = 0;
  size_t o = 0;
  for (int h = 0; h < hops; h++) {
    bool fits = (size_t)((h + 1) * hashBytes) <= pathCap && o + (size_t)hashBytes * 2 + 4 < n;
    if (!fits) {
      if (o + 3 <= n) { memcpy(out + o, "..", 3); }
      return;
    }
    if (h) out[o++] = '>';
    for (int b = 0; b < hashBytes; b++) o += snprintf(out + o, n - o, "%02x", path[h * hashBytes + b]);
  }
}
