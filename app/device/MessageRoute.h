#pragma once
// Fills in a StoredMsg how its packet travelled (MESSAGING-10). Depends on MeshCore only, so app/sim/wire runs this very
// code on real packets.
#include <Packet.h>
#include <helpers/ContactInfo.h>
#include "../core/MessageStore.h"

class MessageRoute {
public:
  // Received from a contact. A flood carries the route it took. A direct packet spends its path on the way, so only the
  // route stored for that contact is left, turned around: it is marked as planned, not taken.
  static void received(StoredMsg& m, const mesh::Packet& pkt, const ContactInfo& from);
  // Received on a channel: the sender is not a known contact, only a flood tells a route.
  static void receivedOnChannel(StoredMsg& m, const mesh::Packet& pkt);
  // About to leave for a contact. A flood finds its way: no route yet.
  static void sending(StoredMsg& m, bool direct, const ContactInfo& to);
};
