#include "NodeRecord.h"
#include <string.h>

namespace {
void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}
uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
}  // namespace

void NodeRecordCodec::encode(const NodeRecord& r, uint8_t* out) {
  memset(out, 0, BYTES);
  memcpy(out, r.pubKey, 32);
  // The name up to its NUL only: the bytes after it stay zero, so two equal names give equal records
  const size_t n = strnlen(r.name, sizeof(r.name) - 1);
  memcpy(out + 32, r.name, n);
  out[64] = r.type;
  out[65] = r.flags;
  out[66] = r.hops;
  out[67] = (uint8_t)r.snrX4;
  out[68] = (uint8_t)r.rssi;
  out[69] = r.posSource;
  put32(out + 72, (uint32_t)r.latE6);
  put32(out + 76, (uint32_t)r.lonE6);
  put32(out + 80, r.posTime);
  put32(out + 84, r.firstHeard);
  put32(out + 88, r.lastHeard);
  put32(out + 92, r.lastAdvertTs);
}

void NodeRecordCodec::decode(const uint8_t* in, NodeRecord& r) {
  memcpy(r.pubKey, in, 32);
  memcpy(r.name, in + 32, sizeof(r.name));
  r.name[sizeof(r.name) - 1] = 0;
  r.type = in[64];
  r.flags = in[65];
  r.hops = in[66];
  r.snrX4 = (int8_t)in[67];
  r.rssi = (int8_t)in[68];
  r.posSource = in[69];
  r.latE6 = (int32_t)get32(in + 72);
  r.lonE6 = (int32_t)get32(in + 76);
  r.posTime = get32(in + 80);
  r.firstHeard = get32(in + 84);
  r.lastHeard = get32(in + 88);
  r.lastAdvertTs = get32(in + 92);
}
