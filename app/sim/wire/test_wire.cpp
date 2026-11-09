// Real MeshCore code on a PC (RADIO-6, RADIO-9, RADIO-10): the files of meshcore/src compiled unchanged, linked with
// app/device/FloodPolicy and the portable PacketLog, on an air simulated in memory. The chat node below floods exactly
// as CardputerMesh does; the repeater uses MeshCore's RegionMap with the decision of simple_repeater/MyMesh.cpp
// (onRecvPacket + allowPacketForward).
//
// What it proves that the fake mesh of the simulator cannot: the bytes we send are valid MeshCore packets, repeaters
// accept or refuse them as expected, and a 2 or 3 byte path comes back and works for direct messages.
// Not covered: the radio itself, and repeaters older than 1.14 (their refusal comes from meshcore/docs/faq.md).
//   make -C app/sim wire
#include <helpers/AdvertDataHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/RegionMap.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/TxtDataHelpers.h>
#include "../../core/drivers/PacketLog.h"
#include "../../device/FloodPolicy.h"
#include "../../device/ContactFile.h"
#include "../../device/ContactPositions.h"
#include "../../device/MessageRoute.h"
#include <map>

#include <deque>
#include <functional>
#include <string>
#include <vector>

// Our copy of the header layout must stay MeshCore's
static_assert(wire::ROUTE_TRANSPORT_FLOOD == ROUTE_TYPE_TRANSPORT_FLOOD && wire::ROUTE_FLOOD == ROUTE_TYPE_FLOOD &&
              wire::ROUTE_DIRECT == ROUTE_TYPE_DIRECT && wire::ROUTE_TRANSPORT_DIRECT == ROUTE_TYPE_TRANSPORT_DIRECT, "routes");
static_assert(wire::PL_REQ == PAYLOAD_TYPE_REQ && wire::PL_RESPONSE == PAYLOAD_TYPE_RESPONSE && wire::PL_TXT == PAYLOAD_TYPE_TXT_MSG &&
              wire::PL_ACK == PAYLOAD_TYPE_ACK && wire::PL_ADVERT == PAYLOAD_TYPE_ADVERT && wire::PL_GRP_TXT == PAYLOAD_TYPE_GRP_TXT &&
              wire::PL_GRP_DATA == PAYLOAD_TYPE_GRP_DATA && wire::PL_ANON_REQ == PAYLOAD_TYPE_ANON_REQ && wire::PL_PATH == PAYLOAD_TYPE_PATH &&
              wire::PL_TRACE == PAYLOAD_TYPE_TRACE && wire::PL_MULTIPART == PAYLOAD_TYPE_MULTIPART && wire::PL_CONTROL == PAYLOAD_TYPE_CONTROL &&
              wire::PL_RAW == PAYLOAD_TYPE_RAW_CUSTOM, "payload types");
static_assert(wire::header(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT) == ((PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD), "header");
static_assert(floodopt::REGION_MAX + 1 == sizeof(RegionEntry::name), "region names");

static int g_checks = 0, g_failed = 0;
static const char* g_test = "";
#define CHECK(c) do { g_checks++; if (!(c)) { g_failed++; printf("  FAIL line %d [%s] %s\n", __LINE__, g_test, #c); } } while (0)

// ---------------------------------------------------------------- the air

static uint32_t g_now = 1000;

class Clock : public mesh::MillisecondClock { public: unsigned long getMillis() override { return g_now; } };
class Rtc : public mesh::RTCClock {
public:
  uint32_t getCurrentTime() override { return 1793541600 + g_now / 1000; }
  void setCurrentTime(uint32_t) override {}
};
class Rng : public mesh::RNG {
public:
  explicit Rng(uint32_t seed) : _s(seed) {}
  void random(uint8_t* d, size_t n) override {
    for (size_t i = 0; i < n; i++) { _s = _s * 1664525u + 1013904223u; d[i] = (uint8_t)(_s >> 16); }
  }
private:
  uint32_t _s;
};

class FakeRadio : public mesh::Radio {
public:
  std::vector<FakeRadio*> hears;            // who receives what this radio sends
  std::deque<std::vector<uint8_t>> inbox;
  int sent = 0;
  std::vector<std::vector<uint8_t>> onAir;  // raw copies of what it sent

  int recvRaw(uint8_t* b, int sz) override {
    if (inbox.empty()) return 0;
    std::vector<uint8_t> f = inbox.front();
    inbox.pop_front();
    memcpy(b, f.data(), f.size());
    return (int)f.size();
  }
  uint32_t getEstAirtimeFor(int) override { return 20; }
  float packetScore(float, int) override { return 1.0f; }
  bool startSendRaw(const uint8_t* b, int n) override {
    sent++;
    std::vector<uint8_t> f(b, b + n);
    onAir.push_back(f);
    for (FakeRadio* r : hears) r->inbox.push_back(f);
    return true;
  }
  bool isSendComplete() override { return true; }
  void onSendFinished() override {}
  bool isInRecvMode() const override { return true; }
  float getLastSNR() const override { return -7.5f; }
  float getLastRSSI() const override { return -105; }
};

static void link(FakeRadio& a, FakeRadio& b) { a.hears.push_back(&b); b.hears.push_back(&a); }

// What a node needs before MeshCore's constructors run (a base class, so it exists first: they read the clock)
struct NodeParts {
  explicit NodeParts(uint32_t seed) : partRng(seed), partPool(16) {}
  Clock partClock;
  Rng partRng;
  Rtc partRtc;
  StaticPoolPacketManager partPool;
  SimpleMeshTables partTables;
};

// ---------------------------------------------------------------- a chat node that floods like CardputerMesh

class ChatNode : private NodeParts, public BaseChatMesh {
public:
  ChatNode(FakeRadio& r, uint32_t seed)
    : NodeParts(seed), BaseChatMesh(r, partClock, partRng, partRtc, partPool, partTables), radio(r) {}
  FakeRadio& radio;
  FloodPolicy floods;
  PacketLog packets;
  std::vector<std::string> inbox;
  StoredMsg lastMsg;                 // the record CardputerMesh would store for the last message received
  ContactPositions positions;        // as in CardputerMesh
  bool replaceWhenFull = false;      // CardputerMesh: true since VALIDATION-11
  std::vector<std::string> replaced;
  int fullCalls = 0;
  int contacts() { return getNumContacts(); }
  void favourite(const ChatNode& other) { contact(other)->flags |= 1; }
  // Speaks as another node from now on: one radio, many identities, to fill a contact list
  void become(uint32_t seed, const char* name) {
    Rng rng(seed);
    self_id = mesh::LocalIdentity(&rng);
    _name = name;
  }
  int acks = 0;
  uint32_t expectedAck = 0;

  void start(const char* name) {
    BaseChatMesh::begin();
    self_id = mesh::LocalIdentity(&partRng);
    _name = name;
  }
  const mesh::LocalIdentity& id() const { return self_id; }
  void advertise() { floods.send(*this, createSelfAdvert(_name.c_str())); }
  mesh::Packet* advertAt(double lat, double lon) { return createSelfAdvert(_name.c_str(), lat, lon); }   // dated now
  void flood(mesh::Packet* p) { floods.send(*this, p); }
  bool discover(const ChatNode& other) { return contact(other) != nullptr; }
  ContactInfo* contact(const ChatNode& other) { return lookupContactByPubKey(other.self_id.pub_key, PUB_KEY_SIZE); }
  int send(ChatNode& to, const char* text) {
    uint32_t timeout = 0;
    return sendMessage(*contact(to), getRTCClock()->getCurrentTime(), 0, text, expectedAck, timeout);
  }
  void joinPublic() { _ch = addChannel("Public", "izOH6cXN6mrJ5e26oRXNcg=="); }
  bool sendChannel(const char* text) { return sendGroupMessage(getRTCClock()->getCurrentTime(), _ch->channel, _name.c_str(), text, strlen(text)); }

protected:
  float getAirtimeBudgetFactor() const override { return 1.0f; }
  int calcRxDelay(float, uint32_t) const override { return 0; }
  bool allowPacketForward(const mesh::Packet*) override { return false; }
  // The two overrides of CardputerMesh
  void sendFloodScoped(const ContactInfo&, mesh::Packet* p, uint32_t d) override { floods.send(*this, p, d); }
  void sendFloodScoped(const mesh::GroupChannel&, mesh::Packet* p, uint32_t d) override { floods.send(*this, p, d); }
  void logRx(mesh::Packet* p, int len, float) override { record(*p, len, false); }
  void logTx(mesh::Packet* p, int len) override { record(*p, len, true); }

  void onDiscoveredContact(ContactInfo&, bool, uint8_t, const uint8_t*) override {}
  bool shouldOverwriteWhenFull() const override { return replaceWhenFull; }
  void onContactOverwrite(const uint8_t* key) override {
    if (ContactInfo* c = lookupContactByPubKey(key, PUB_KEY_SIZE)) replaced.push_back(c->name);
    positions.book().remove(key);
  }
  void onContactsFull() override { fullCalls++; }
  void onContactPathUpdated(const ContactInfo&) override {}
  ContactInfo* processAck(const uint8_t* data) override {
    if (memcmp(data, &expectedAck, 4) == 0) acks++;
    return nullptr;
  }
  void onMessageRecv(const ContactInfo& from, mesh::Packet* p, uint32_t ts, const char* text) override {
    inbox.push_back(text);
    lastMsg = StoredMsg();
    MessageRoute::received(lastMsg, *p, from);
    bool found;
    if (ContactInfo* c = lookupContactByPubKey(from.id.pub_key, PUB_KEY_SIZE)) positions.fromMessage(*c, text, ts, found);
  }
  void onAdvertRecv(mesh::Packet* p, const mesh::Identity& id, uint32_t ts, const uint8_t* data, size_t len) override {
    const ContactPositions::Before before = ContactPositions::before(lookupContactByPubKey(id.pub_key, PUB_KEY_SIZE));
    BaseChatMesh::onAdvertRecv(p, id, ts, data, len);
    positions.afterAdvert(before, lookupContactByPubKey(id.pub_key, PUB_KEY_SIZE), ts, data, len);
  }
  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onCLICommandRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*, char*) override {}
  void onSignedMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const uint8_t*, const char*) override {}
  uint32_t calcFloodTimeoutMillisFor(uint32_t) const override { return 2000; }
  uint32_t calcDirectTimeoutMillisFor(uint32_t, uint8_t) const override { return 2000; }
  void onSendTimeout() override {}
  void onChannelMessageRecv(const mesh::GroupChannel&, mesh::Packet* p, uint32_t, const char* text) override {
    inbox.push_back(text);
    lastMsg = StoredMsg();
    MessageRoute::receivedOnChannel(lastMsg, *p);
  }
  uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t, uint8_t*) override { return 0; }
  void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override {}

private:
  void record(const mesh::Packet& p, int len, bool tx) {   // CardputerMesh::recordPacket
    PacketLog::Frame f;
    f.header = p.header;
    f.pathLen = (uint8_t)p.path_len;
    f.path = p.path;
    f.payload = p.payload;
    f.payloadLen = p.payload_len;
    f.transportCode = p.transport_codes[0];
    f.rawLen = len;
    f.snr = p.getSNR();
    f.tx = tx;
    f.ourRegion = floods.isOurRegion(p);
    packets.add(f);
  }
  std::string _name;
  ChannelDetails* _ch = nullptr;
};

// ---------------------------------------------------------------- a repeater that decides like simple_repeater

class Repeater : private NodeParts, public mesh::Mesh {
public:
  explicit Repeater(FakeRadio& r, uint32_t seed)
    : NodeParts(seed), mesh::Mesh(r, partClock, partRng, partRtc, partPool, partTables), _regions(_keys) {}
  void start() { begin(); self_id = mesh::LocalIdentity(&partRng); }
  RegionMap& regions() { return _regions; }
  const mesh::LocalIdentity& id() const { return self_id; }
protected:
  float getAirtimeBudgetFactor() const override { return 1.0f; }
  uint32_t getRetransmitDelay(const mesh::Packet*) override { return 10; }
  uint32_t getDirectRetransmitDelay(const mesh::Packet*) override { return 10; }
  int calcRxDelay(float, uint32_t) const override { return 0; }
  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override {
    if (pkt->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) _recv = _regions.findMatch(pkt, REGION_DENY_FLOOD);
    else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) _recv = (_regions.getWildcard().flags & REGION_DENY_FLOOD) ? nullptr : &_regions.getWildcard();
    else _recv = nullptr;
    return mesh::Mesh::onRecvPacket(pkt);
  }
  bool allowPacketForward(const mesh::Packet* p) override { return !(p->isRouteFlood() && !_recv); }
private:
  TransportKeyStore _keys;
  RegionMap _regions;
  RegionEntry* _recv = nullptr;
};

// CLI of a real repeater: "region put <name>" then "region allowf <name>" (a new region is refused by default)
static void allow(RegionMap& map, const char* name) { map.putRegion(name, 0)->flags &= ~REGION_DENY_FLOOD; }

// A -- R -- B: A and B only reach each other through R
struct Net {
  FakeRadio ra, rb, rr;
  ChatNode a{ra, 11}, b{rb, 22};
  Repeater r{rr, 33};
  Net() {
    link(ra, rr);
    link(rb, rr);
    a.start("Alice");
    b.start("Bob");
    r.start();
  }
  void run(int ms = 3000) {
    for (int t = 0; t < ms; t += 10) {
      g_now += 10;
      a.loop();
      r.loop();
      b.loop();
    }
  }
  void meet() {
    a.advertise();
    run();
    b.advertise();
    run();
  }
};

// A -- R1 -- R2 -- B: the order of the hops can only be seen with two repeaters
struct Chain {
  FakeRadio ra, rb, r1, r2;
  ChatNode a{ra, 11}, b{rb, 22};
  Repeater first{r1, 33}, second{r2, 44};
  Chain() {
    link(ra, r1);
    link(r1, r2);
    link(r2, rb);
    a.start("Alice");
    b.start("Bob");
    first.start();
    second.start();
  }
  void run(int ms = 3000) {
    for (int t = 0; t < ms; t += 10) {
      g_now += 10;
      a.loop();
      first.loop();
      second.loop();
      b.loop();
    }
  }
  void meet() {
    a.advertise();
    run();
    b.advertise();
    run();
  }
};

static std::vector<std::pair<const char*, std::function<void()>>> g_tests;
struct Reg { Reg(const char* n, std::function<void()> f) { g_tests.push_back({n, f}); } };
#define TEST(n) static void n(); static Reg reg_##n(#n, n); static void n()

// ---------------------------------------------------------------- the tests

TEST(identities_sign_and_verify) {
  Rng rng(5);
  mesh::LocalIdentity id(&rng);
  uint8_t sig[SIGNATURE_SIZE];
  const uint8_t msg[] = "cardputer";
  id.sign(sig, msg, sizeof(msg));
  mesh::Identity pub(id.pub_key);
  CHECK(pub.verify(sig, msg, sizeof(msg)));
  sig[3] ^= 1;
  CHECK(!pub.verify(sig, msg, sizeof(msg)));
}

TEST(region_key_is_the_one_repeaters_derive) {
  uint8_t expected[16];
  mesh::Utils::sha256(expected, 16, (const uint8_t*)"#fr", 3);   // computed independently of FloodPolicy
  TransportKey k;
  memcpy(k.key, expected, 16);
  mesh::Packet p;
  p.header = wire::header(wire::ROUTE_FLOOD, wire::PL_TXT);
  p.payload_len = 3;
  memcpy(p.payload, "abc", 3);
  p.transport_codes[0] = k.calcTransportCode(&p);
  p.header = wire::header(wire::ROUTE_TRANSPORT_FLOOD, wire::PL_TXT);
  FloodPolicy fr, frHash, upper, none;
  fr.configure(1, "fr");
  frHash.configure(1, "#fr");
  upper.configure(1, "FR");
  none.configure(1, "");
  CHECK(fr.isOurRegion(p));
  CHECK(frHash.isOurRegion(p));
  CHECK(!upper.isOurRegion(p));
  CHECK(!none.isOurRegion(p));

  // Both spellings of a region on a repeater, with MeshCore's own matching
  TransportKeyStore k1, k2, k3;
  RegionMap plain(k1), tagged(k2), other(k3);
  allow(plain, "fr");
  allow(tagged, "#fr");
  allow(other, "eu");
  CHECK(plain.findMatch(&p, REGION_DENY_FLOOD) != nullptr);
  CHECK(tagged.findMatch(&p, REGION_DENY_FLOOD) != nullptr);
  CHECK(other.findMatch(&p, REGION_DENY_FLOOD) == nullptr);
}

TEST(configure_follows_the_settings) {
  FloodPolicy f;
  CHECK(f.pathHashBytes() == 1 && !f.scoped());
  f.configure(2, "fr");
  CHECK(f.pathHashBytes() == 2 && f.scoped() && !strcmp(f.region(), "fr"));
  f.configure(9, "$private");      // damaged settings: the safe values
  CHECK(f.pathHashBytes() == 1 && !f.scoped());
  f.configure(3, "fr");
  f.configure(3, "");
  CHECK(!f.scoped());
}

// Every size with and without region: what leaves the node, what the repeater adds, what the far node hears
TEST(floods_with_every_size_and_region) {
  for (int bytes = 1; bytes <= 3; bytes++) {
    for (const char* region : {"", "fr"}) {
      Net n;
      allow(n.r.regions(), "fr");
      n.a.floods.configure(bytes, region);
      n.a.advertise();
      n.run();
      CHECK(n.ra.sent == 1);
      const std::vector<uint8_t>& raw = n.ra.onAir[0];
      bool scoped = region[0];
      CHECK((raw[0] & PH_ROUTE_MASK) == (scoped ? ROUTE_TYPE_TRANSPORT_FLOOD : ROUTE_TYPE_FLOOD));
      uint8_t pathByte = raw[scoped ? 5 : 1];
      CHECK((pathByte >> 6) + 1 == bytes);
      CHECK((pathByte & 63) == 0);

      CHECK(n.b.discover(n.a));              // signature, parsing, everything on the real path
      const std::vector<uint8_t>& fwd = n.rr.onAir.at(0);
      uint8_t fwdPath = fwd[scoped ? 5 : 1];
      CHECK((fwdPath >> 6) + 1 == bytes);
      CHECK((fwdPath & 63) == 1);
      CHECK(memcmp(&fwd[scoped ? 6 : 2], n.r.id().pub_key, bytes) == 0);   // the repeater's ID on `bytes` bytes

      const PacketRecord* r = n.b.packets.recent(0);
      CHECK(r && r->type == wire::PL_ADVERT && r->hops == 1 && r->hashBytes == bytes);
      CHECK(r->scoped() == scoped);
      CHECK(memcmp(r->lastHop, n.r.id().pub_key, bytes) == 0);
      CHECK(r->fromLen == 3 && memcmp(r->from, n.a.id().pub_key, 3) == 0);
      CHECK(r->source == PacketRecord::SRC_NODE);
    }
  }
}

TEST(repeaters_decide_on_the_region) {
  struct Case { const char* sender; const char* repeater; bool denyUnscoped; bool heard; };
  const Case cases[] = {
    {"", nullptr, false, true},      // no region anywhere: as today
    {"", "fr", false, true},         // a region-aware repeater still forwards plain floods
    {"", "fr", true, false},         // ...unless it refuses unscoped floods
    {"fr", "fr", true, true},        // the reason to set a region
    {"fr", "fr", false, true},
    {"fr", "eu", false, false},      // the wrong name stops the flood at the first hop
    {"fr", nullptr, false, false},   // and so does a repeater without regions
    {"FR", "fr", false, false},      // the case counts
  };
  for (const Case& c : cases) {
    Net n;
    if (c.repeater) allow(n.r.regions(), c.repeater);
    if (c.denyUnscoped) n.r.regions().getWildcard().flags |= REGION_DENY_FLOOD;
    n.a.floods.configure(1, c.sender);
    n.a.advertise();
    n.run();
    if (n.b.discover(n.a) != c.heard) printf("  case sender '%s' repeater '%s' deny %d\n", c.sender, c.repeater ? c.repeater : "-", c.denyUnscoped);
    CHECK(n.b.discover(n.a) == c.heard);
  }
}

// The whole path of a message: flood out, path returned, then direct with the stored 1, 2 or 3 byte path
TEST(messages_find_a_route_with_every_size) {
  for (int bytes = 1; bytes <= 3; bytes++) {
    Net n;
    allow(n.r.regions(), "fr");
    n.r.regions().getWildcard().flags |= REGION_DENY_FLOOD;
    n.a.floods.configure(bytes, "fr");
    n.b.floods.configure(bytes, "fr");
    n.meet();
    CHECK(n.a.discover(n.b) && n.b.discover(n.a));
    CHECK(n.a.contact(n.b)->out_path_len == OUT_PATH_UNKNOWN);

    CHECK(n.a.send(n.b, "first") == MSG_SEND_SENT_FLOOD);
    n.run();
    CHECK(n.b.inbox.size() == 1 && n.b.inbox[0] == "first");
    CHECK(n.a.acks == 1);
    uint8_t path = n.a.contact(n.b)->out_path_len;
    CHECK(path != OUT_PATH_UNKNOWN);
    CHECK((path >> 6) + 1 == bytes);
    CHECK((path & 63) == 1);
    CHECK(memcmp(n.a.contact(n.b)->out_path, n.r.id().pub_key, bytes) == 0);

    CHECK(n.a.send(n.b, "second") == MSG_SEND_SENT_DIRECT);
    n.run();
    CHECK(n.b.inbox.size() == 2 && n.b.inbox[1] == "second");
    CHECK(n.a.acks == 2);

    const PacketRecord* sent = nullptr;
    for (int i = 0; i < n.a.packets.count() && !sent; i++) {
      const PacketRecord* r = n.a.packets.recent(i);
      if (r->tx() && r->type == wire::PL_TXT) sent = r;
    }
    CHECK(sent && sent->route == wire::ROUTE_DIRECT && sent->hashBytes == bytes);
    CHECK(sent->from[0] == n.a.id().pub_key[0] && sent->to == n.b.id().pub_key[0]);
  }
}

TEST(channel_messages_carry_the_settings) {
  Net n;
  allow(n.r.regions(), "fr");
  n.a.joinPublic();
  n.b.joinPublic();
  n.a.floods.configure(2, "fr");
  n.b.floods.configure(1, "fr");
  CHECK(n.a.sendChannel("hello"));
  n.run();
  CHECK(n.b.inbox.size() == 1 && n.b.inbox[0].find("hello") != std::string::npos);
  const PacketRecord* r = n.b.packets.recent(0);
  CHECK(r->type == wire::PL_GRP_TXT && r->hops == 1 && r->hashBytes == 2);
  CHECK(r->source == PacketRecord::SRC_CHANNEL && r->fromLen == 1);
  CHECK(r->scoped() && r->ourRegion());    // B's region is the sender's: the log shows it
}

TEST(a_region_not_allowed_on_the_repeater_is_refused) {
  Net n;
  n.r.regions().putRegion("fr", 0);       // put, but no "region allowf fr"
  n.a.floods.configure(1, "fr");
  n.a.advertise();
  n.run();
  CHECK(!n.b.discover(n.a));
}

// The route of a flood is written from the sender to us, and our ACK and returned path use it the other way
TEST(a_flood_records_its_real_route_in_order) {
  for (int bytes = 1; bytes <= 3; bytes++) {
    Chain n;
    n.a.floods.configure(bytes, "");
    n.b.floods.configure(bytes, "");
    n.meet();
    CHECK(n.a.send(n.b, "first") == MSG_SEND_SENT_FLOOD);
    n.run();
    CHECK(n.b.inbox.size() == 1);
    const StoredMsg& m = n.b.lastMsg;
    CHECK(m.route.kind == PATH_TAKEN);
    CHECK(m.routeType == ROUTE_TYPE_FLOOD);
    CHECK(m.hops == 2);
    PathView v = m.route.view();
    CHECK(v.hops() == 2 && v.hashBytes() == bytes && !v.truncated());
    CHECK(memcmp(v.hash(0), n.first.id().pub_key, bytes) == 0);     // the repeater next to the sender comes first
    CHECK(memcmp(v.hash(1), n.second.id().pub_key, bytes) == 0);    // the last one is the one we heard
  }
}

TEST(a_direct_message_only_has_the_planned_route) {
  for (int bytes = 1; bytes <= 3; bytes++) {
    Chain n;
    n.a.floods.configure(bytes, "");
    n.b.floods.configure(bytes, "");
    n.meet();
    CHECK(n.a.send(n.b, "flood") == MSG_SEND_SENT_FLOOD);
    n.run();
    n.b.send(n.a, "reply");            // B learns its route to A from the path A returns
    n.run();
    CHECK(n.b.contact(n.a)->out_path_len != OUT_PATH_UNKNOWN);
    CHECK(n.a.send(n.b, "direct") == MSG_SEND_SENT_DIRECT);
    n.run();
    CHECK(n.b.inbox.back() == "direct");
    const StoredMsg& m = n.b.lastMsg;
    CHECK(m.routeType == ROUTE_TYPE_DIRECT);
    CHECK(m.route.kind == PATH_PLANNED);                            // not proof of the way it came
    PathView v = m.route.view();
    CHECK(v.hops() == 2 && v.hashBytes() == bytes);
    CHECK(memcmp(v.hash(0), n.first.id().pub_key, bytes) == 0);     // travel order, sender's side first
    CHECK(memcmp(v.hash(1), n.second.id().pub_key, bytes) == 0);
    StoredMsg sent;
    MessageRoute::sending(sent, true, *n.a.contact(n.b));
    CHECK(sent.route.kind == PATH_PLANNED);
    CHECK(memcmp(sent.route.view().hash(0), n.first.id().pub_key, bytes) == 0);   // our side first
    MessageRoute::sending(sent, false, *n.a.contact(n.b));
    CHECK(sent.route.kind == PATH_NONE);
  }
}

TEST(a_channel_message_records_its_route) {
  Chain n;
  n.a.joinPublic();
  n.b.joinPublic();
  n.a.floods.configure(2, "");
  CHECK(n.a.sendChannel("hello"));
  n.run();
  const StoredMsg& m = n.b.lastMsg;
  CHECK(m.route.kind == PATH_TAKEN && m.hops == 2);
  PathView v = m.route.view();
  CHECK(v.hashBytes() == 2);
  CHECK(memcmp(v.hash(0), n.first.id().pub_key, 2) == 0);
  CHECK(memcmp(v.hash(1), n.second.id().pub_key, 2) == 0);
}

TEST(the_log_tells_another_region_from_ours) {
  Net n;
  allow(n.r.regions(), "fr");
  allow(n.r.regions(), "eu");
  n.a.floods.configure(1, "fr");
  n.b.floods.configure(1, "eu");
  n.a.advertise();
  n.run();
  const PacketRecord* r = n.b.packets.recent(0);
  CHECK(r->scoped() && !r->ourRegion());
  CHECK(r->transportCode != 0);
}

// ---------------------------------------------------------------- the contacts file (MESSAGING-11)

class MemoryStorage : public Storage {
public:
  bool read(const char* path, uint32_t off, void* buf, uint32_t len) override {
    auto it = files.find(path);
    if (it == files.end() || off + len > it->second.size()) return false;
    memcpy(buf, it->second.data() + off, len);
    return true;
  }
  bool write(const char* path, uint32_t off, const void* buf, uint32_t len) override {
    if (failWrites) return false;
    std::vector<uint8_t>& f = files[path];
    if (f.size() < off + len) f.resize(off + len);
    memcpy(f.data() + off, buf, len);
    return true;
  }
  bool remove(const char* path) override { return files.erase(path) > 0; }
  bool rename(const char* from, const char* to) override {
    if (failRename) return false;
    auto it = files.find(from);
    if (it == files.end() || files.count(to)) return false;
    files[to] = it->second;
    files.erase(from);
    return true;
  }
  std::map<std::string, std::vector<uint8_t>> files;
  bool failWrites = false, failRename = false;
};

static ContactEntry someContact(uint8_t seed, const char* name) {
  Rng rng(seed);
  mesh::LocalIdentity id(&rng);
  ContactEntry e;
  memset(&e.info, 0, sizeof(e.info));
  e.info.id = mesh::Identity(id.pub_key);
  strcpy(e.info.name, name);
  e.info.type = ADV_TYPE_CHAT;
  e.info.flags = 1;
  e.info.out_path_len = (1 << 6) | 2;
  e.info.out_path[0] = 0xA1;
  e.info.out_path[3] = 0xD4;
  e.info.last_advert_timestamp = 1793541000 + seed;
  e.info.lastmod = 1793541600;
  e.info.gps_lat = 43604700 + seed;
  e.info.gps_lon = 1444200;
  e.position.time = 1793541500 + seed;
  e.position.source = PositionSource::Message;
  return e;
}

static int loadAll(ContactFile& f, std::vector<ContactEntry>& out) {
  out.clear();
  return f.load([&](const ContactEntry& e) { out.push_back(e); return true; });
}

TEST(contacts_file_round_trip) {
  MemoryStorage disk;
  ContactFile f(disk, "/contacts.cp");
  CHECK(f.begin());
  for (int i = 0; i < 9; i++) CHECK(f.add(someContact((uint8_t)i, i == 4 ? "Bob" : "Someone")));   // spans chunks
  CHECK(f.commit());
  CHECK(!disk.files.count("/contacts.cp.new"));
  std::vector<ContactEntry> got;
  CHECK(loadAll(f, got) == 9);
  if (got.size() != 9) return;           // nothing more can be read safely
  CHECK(f.lastFormat() == 2);
  const ContactEntry want = someContact(4, "Bob");
  const ContactEntry& e = got[4];
  CHECK(!strcmp(e.info.name, "Bob"));
  CHECK(e.info.id.matches(want.info.id));
  CHECK(e.info.out_path_len == want.info.out_path_len && e.info.out_path[3] == 0xD4);
  CHECK(e.info.gps_lat == want.info.gps_lat && e.info.flags == 1);
  CHECK(e.position.time == want.position.time && e.position.source == PositionSource::Message);
  // a smaller list replaces the bigger one entirely
  CHECK(f.begin());
  CHECK(f.add(someContact(1, "Only")));
  CHECK(f.commit());
  CHECK(loadAll(f, got) == 1 && !strcmp(got[0].info.name, "Only"));
}

TEST(contacts_file_reads_version_1) {
  MemoryStorage disk;
  // A file written by the previous firmware: magic, then 148-byte records until the end
  std::vector<uint8_t> v1(4);
  uint32_t magic = ContactFile::MAGIC_V1;
  memcpy(v1.data(), &magic, 4);
  for (int i = 0; i < 3; i++) {
    ContactEntry e = someContact((uint8_t)(10 + i), "Old");
    uint8_t rec[148] = {};
    memcpy(rec, e.info.id.pub_key, 32);
    memcpy(rec + 32, e.info.name, 32);
    rec[64] = e.info.type;
    rec[66] = e.info.out_path_len;
    memcpy(rec + 132, &e.info.last_advert_timestamp, 4);
    memcpy(rec + 140, &e.info.gps_lat, 4);
    memcpy(rec + 144, &e.info.gps_lon, 4);
    if (i == 2) memset(rec + 140, 0, 8);    // no position
    v1.insert(v1.end(), rec, rec + sizeof(rec));
  }
  disk.files["/contacts.cp"] = v1;
  ContactFile f(disk, "/contacts.cp");
  std::vector<ContactEntry> got;
  CHECK(loadAll(f, got) == 3);
  if (got.size() != 3) return;
  CHECK(f.lastFormat() == 1);
  CHECK(got[0].info.id.matches(someContact(10, "").info.id));
  CHECK(got[0].position.source == PositionSource::Advert);           // dated by its advert
  CHECK(got[0].position.time == got[0].info.last_advert_timestamp);
  CHECK(got[2].position.source == PositionSource::None);
  // and the next save writes version 2
  CHECK(f.begin());
  for (auto& e : got) CHECK(f.add(e));
  CHECK(f.commit());
  CHECK(loadAll(f, got) == 3 && f.lastFormat() == 2);
}

TEST(contacts_file_survives_an_interrupted_save) {
  MemoryStorage disk;
  ContactFile f(disk, "/contacts.cp");
  f.begin();
  f.add(someContact(1, "Kept"));
  CHECK(f.commit());
  // the device stops while writing the new list: the old one is still read
  disk.files["/contacts.cp.new"] = std::vector<uint8_t>(40, 0x55);
  std::vector<ContactEntry> got;
  CHECK(loadAll(f, got) == 1 && !strcmp(got[0].info.name, "Kept"));
  // a full write that cannot finish leaves the old list
  f.begin();
  f.add(someContact(2, "New"));
  disk.failWrites = true;
  CHECK(!f.commit());
  disk.failWrites = false;
  CHECK(loadAll(f, got) == 1 && !strcmp(got[0].info.name, "Kept"));
  // it stops between removing the old file and renaming the new one: the new one is read
  f.begin();
  f.add(someContact(3, "Renamed"));
  disk.failRename = true;
  CHECK(!f.commit());
  disk.failRename = false;
  CHECK(!disk.files.count("/contacts.cp"));
  CHECK(loadAll(f, got) == 1 && !strcmp(got[0].info.name, "Renamed"));
  if (got.empty()) return;
  // the next save puts things back in order
  f.begin();
  f.add(got[0]);
  CHECK(f.commit());
  CHECK(disk.files.count("/contacts.cp") && !disk.files.count("/contacts.cp.new"));
}

TEST(contacts_file_stops_when_the_list_is_full) {
  MemoryStorage disk;
  ContactFile f(disk, "/contacts.cp");
  f.begin();
  for (int i = 0; i < 5; i++) f.add(someContact((uint8_t)i, "C"));
  f.commit();
  int taken = 0;
  f.load([&](const ContactEntry&) { return ++taken < 2; });
  CHECK(taken == 2);
  MemoryStorage empty;
  ContactFile none(empty, "/contacts.cp");
  std::vector<ContactEntry> got;
  CHECK(loadAll(none, got) == 0);
  empty.files["/contacts.cp"] = {1, 2, 3};   // too short to be anything
  CHECK(loadAll(none, got) == 0);
}

// ---------------------------------------------------------------- positions from adverts and messages (MESSAGING-11)

TEST(the_newest_position_wins_whatever_arrives_last) {
  Net n;
  n.a.advertise();
  n.run();
  n.b.flood(n.b.advertAt(43.60, 1.44));                  // Bob's advert with a position
  n.run();
  ContactInfo* bob = n.a.contact(n.b);
  CHECK(bob && bob->gps_lat == 43600000);
  CHECK(n.a.positions.meta(*bob).source == PositionSource::Advert);

  mesh::Packet* late = n.b.advertAt(48.85, 2.29);         // an advert made now...
  g_now += 5000;
  n.b.send(n.a, "here: geo:43.6500,1.5000");              // ...then a message, 5 s later by Bob's clock
  n.run();
  CHECK(bob->gps_lat == 43650000 && bob->gps_lon == 1500000);
  CHECK(n.a.positions.meta(*bob).source == PositionSource::Message);

  n.b.flood(late);                                        // the older advert arrives last
  n.run();
  CHECK(bob->last_advert_timestamp != 0);
  CHECK(bob->gps_lat == 43650000);                        // MeshCore took the advert, its position did not win
  CHECK(n.a.positions.meta(*bob).source == PositionSource::Message);

  g_now += 5000;
  n.b.flood(n.b.advertAt(44.00, 2.00));                   // a newer advert does
  n.run();
  CHECK(bob->gps_lat == 44000000);
  CHECK(n.a.positions.meta(*bob).source == PositionSource::Advert);

  n.b.flood(n.b.advertAt(0, 0));                          // an advert without position keeps the known one
  g_now += 2000;
  n.run();
  CHECK(bob->gps_lat == 44000000);
}

TEST(only_a_direct_message_moves_a_contact) {
  Net n;
  n.meet();
  n.a.joinPublic();
  n.b.joinPublic();
  ContactInfo* bob = n.a.contact(n.b);
  CHECK(bob->gps_lat == 0);
  n.b.sendChannel("I am at geo:45.0,5.0");                // a channel message only carries a name
  n.run();
  CHECK(bob->gps_lat == 0);
  n.b.send(n.a, "1.5, 2.5 km away");                      // not a position
  n.run();
  CHECK(bob->gps_lat == 0);
  n.b.send(n.a, "geo:45.0,5.0");
  n.run();
  CHECK(bob->gps_lat == 45000000);
}

// ---------------------------------------------------------------- a full contact list (VALIDATION-11)

// Fills A's list with `count` contacts heard one after the other (the first heard is the oldest)
static void fill(Net& n, int count) {
  for (int i = 0; i < count; i++) {
    char name[16];
    snprintf(name, sizeof(name), "Node%02d", i);
    n.b.become(1000 + i, name);
    n.b.advertise();
    g_now += 1500;                 // a different "last heard" second for each
    n.run(500);
  }
}

TEST(a_full_list_refused_new_contacts) {        // the bug reported on 2026-11-08, as it was
  Net n;
  fill(n, MAX_CONTACTS);
  CHECK(n.a.contacts() == MAX_CONTACTS);
  n.b.become(77, "Friend");
  n.b.advertise();
  n.run();
  CHECK(!n.a.discover(n.b));                    // refused
  CHECK(n.a.fullCalls == 1);
}

TEST(a_full_list_replaces_the_oldest_non_favourite) {
  Net n;
  n.a.replaceWhenFull = true;
  fill(n, MAX_CONTACTS);
  n.b.become(1000, "Node00");                   // the oldest is a favourite: protected
  n.a.favourite(n.b);
  n.b.become(77, "Friend");
  n.b.advertise();
  n.run();
  CHECK(n.a.discover(n.b));
  CHECK(n.a.contacts() == MAX_CONTACTS);
  CHECK(n.a.replaced.size() == 1 && n.a.replaced[0] == "Node01");
  n.b.become(1000, "Node00");
  CHECK(n.a.discover(n.b));
  n.b.become(1001, "Node01");
  CHECK(!n.a.discover(n.b));
  // and the friend's messages can now be read
  n.b.become(77, "Friend");
  n.a.advertise();                              // so that the friend knows A
  n.run();
  CHECK(n.b.discover(n.a));
  n.b.send(n.a, "can you read me?");
  n.run();
  CHECK(!n.a.inbox.empty() && n.a.inbox.back() == "can you read me?");
}

TEST(a_list_of_favourites_only_still_refuses) {
  Net n;
  n.a.replaceWhenFull = true;
  fill(n, MAX_CONTACTS);
  for (int i = 0; i < MAX_CONTACTS; i++) {
    char name[16];
    snprintf(name, sizeof(name), "Node%02d", i);
    n.b.become(1000 + i, name);
    n.a.favourite(n.b);
  }
  n.b.become(77, "Friend");
  n.b.advertise();
  n.run();
  CHECK(!n.a.discover(n.b));
  CHECK(n.a.replaced.empty());
  CHECK(n.a.fullCalls == 1);                    // the firmware is told, and tells the user
}

int main() {
  for (auto& t : g_tests) {
    g_test = t.first;
    int before = g_failed;
    t.second();
    printf("%s %s\n", g_failed == before ? "ok  " : "FAIL", t.first);
  }
  printf("== wire tests (real MeshCore): %d tests, %d checks, %d failure(s)\n", (int)g_tests.size(), g_checks, g_failed);
  return g_failed ? 1 : 0;
}
