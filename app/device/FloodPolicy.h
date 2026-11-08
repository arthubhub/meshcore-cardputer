#pragma once
// Applies the flood settings of core/drivers/FloodOptions.h to MeshCore packets (RADIO-9, RADIO-10): every flood this
// node starts goes through send(), like MyMesh::sendFloodScoped in meshcore/examples/companion_radio.
// Depends on MeshCore only, not on the firmware around it: app/sim/wire runs it against real MeshCore code on a PC.
#include <Mesh.h>
#include <helpers/TransportKeyStore.h>
#include "../core/drivers/FloodOptions.h"

class FloodPolicy {
public:
  // Cheap when nothing changed, so it can follow the settings on every flood.
  void configure(uint8_t pathHashBytes, const char* region);

  uint8_t pathHashBytes() const { return _pathBytes; }
  const char* region() const { return _region; }
  bool scoped() const { return _region[0] != 0; }

  void send(mesh::Mesh& mesh, mesh::Packet* pkt, uint32_t delayMs = 0) const;
  // A packet carries the transport code of our region. Measured on what the neighbours really send, so it tells
  // whether the region name is the one used locally.
  bool isOurRegion(const mesh::Packet& pkt) const;

private:
  uint8_t _pathBytes = floodopt::PATH_BYTES_MIN;
  char _region[floodopt::REGION_MAX + 1] = "";
  TransportKey _key = {};
};
