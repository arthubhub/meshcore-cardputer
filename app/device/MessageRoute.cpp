#if !CPADV_DEMO   // radio only: the demo firmware has no MeshCore mesh
#include "MessageRoute.h"

static void common(StoredMsg& m, const mesh::Packet& pkt) {
  m.snrX4 = (int8_t)(pkt.getSNR() * 4);
  m.routeType = pkt.getRouteType();
}

void MessageRoute::received(StoredMsg& m, const mesh::Packet& pkt, const ContactInfo& from) {
  common(m, pkt);
  if (pkt.isRouteFlood()) {
    m.hops = pkt.getPathHashCount();
    m.route.set(PATH_TAKEN, (uint8_t)pkt.path_len, pkt.path);
  } else if (from.out_path_len == OUT_PATH_UNKNOWN) {
    m.hops = 0xFF;
  } else {
    m.hops = from.out_path_len & 63;
    m.route.setReversed(PATH_PLANNED, from.out_path_len, from.out_path);
  }
}

void MessageRoute::receivedOnChannel(StoredMsg& m, const mesh::Packet& pkt) {
  common(m, pkt);
  m.hops = pkt.isRouteFlood() ? pkt.getPathHashCount() : 0;
  if (pkt.isRouteFlood()) m.route.set(PATH_TAKEN, (uint8_t)pkt.path_len, pkt.path);
}

void MessageRoute::sending(StoredMsg& m, bool direct, const ContactInfo& to) {
  if (direct) m.route.set(PATH_PLANNED, to.out_path_len, to.out_path);
  else m.route = MsgRoute();
}
#endif  // !CPADV_DEMO
