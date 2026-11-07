#if !CPADV_DEMO   // radio only: the demo firmware has no MeshCore mesh
#include "FloodPolicy.h"
#include <stdio.h>
#include <string.h>

void FloodPolicy::configure(uint8_t pathHashBytes, const char* region) {
  _pathBytes = floodopt::validPathBytes(pathHashBytes);
  char name[sizeof(_region)];
  if (floodopt::normalizeRegion(region, name, sizeof(name)) != floodopt::RegionName::Ok) name[0] = 0;
  if (strcmp(name, _region) == 0) return;
  strcpy(_region, name);
  memset(&_key, 0, sizeof(_key));
  if (!name[0]) return;
  // A public region's key comes from "#name". A local store, as in companion_radio: its cache is keyed by a region id.
  char tag[sizeof(_region) + 1];
  snprintf(tag, sizeof(tag), "#%s", name);
  TransportKeyStore derive;
  derive.getAutoKeyFor(0, tag, _key);
}

void FloodPolicy::send(mesh::Mesh& mesh, mesh::Packet* pkt, uint32_t delayMs) const {
  if (!scoped()) {
    mesh.sendFlood(pkt, delayMs, _pathBytes);
    return;
  }
  uint16_t codes[2] = {_key.calcTransportCode(pkt), 0};   // [1] is reserved, as in companion_radio
  mesh.sendFlood(pkt, codes, delayMs, _pathBytes);
}

bool FloodPolicy::isOurRegion(const mesh::Packet& pkt) const {
  return scoped() && pkt.hasTransportCodes() && pkt.transport_codes[0] == _key.calcTransportCode(&pkt);
}

#endif  // !CPADV_DEMO
