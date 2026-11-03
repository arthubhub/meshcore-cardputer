#include "CardputerMesh.h"
#include <CpadvLog.h>
#if !CPADV_DEMO
#include "../core/RadioPresets.h"
#include <LittleFS.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/IdentityStore.h>
#include <helpers/StaticPoolPacketManager.h>
#include <target.h>

namespace {
ArduinoMillis g_millis;
StaticPoolPacketManager g_packets(16);

constexpr const char* CONTACTS_PATH = "/contacts.cp";
constexpr const char* CHANNELS_PATH = "/channels.cp";
constexpr uint32_t CONTACTS_MAGIC = 0x31434350;   // "PCC1"
constexpr uint32_t CHANNELS_MAGIC = 0x31484350;   // "PCH1"
constexpr const char* PUBLIC_PSK_B64 = "izOH6cXN6mrJ5e26oRXNcg==";   // MeshCore "Public" channel

// Persisted contact record (only what is needed to restore a ContactInfo)
struct ContactRecord {
  uint8_t pubKey[PUB_KEY_SIZE];
  char name[32];
  uint8_t type, flags, outPathLen, reserved;
  uint8_t outPath[MAX_PATH_SIZE];
  uint32_t lastAdvertTs, lastmod;
  int32_t lat, lon;
};

struct ChannelRecord {
  char name[32];
  uint8_t secret[32];
  uint8_t len;
};

uint32_t fnv(const void* p, size_t n, uint32_t h = 2166136261u) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
  return h;
}

void base64(const uint8_t* in, int len, char* out) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int o = 0;
  for (int i = 0; i < len; i += 3) {
    uint32_t v = in[i] << 16 | (i + 1 < len ? in[i + 1] << 8 : 0) | (i + 2 < len ? in[i + 2] : 0);
    out[o++] = T[(v >> 18) & 63];
    out[o++] = T[(v >> 12) & 63];
    out[o++] = i + 1 < len ? T[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < len ? T[v & 63] : '=';
  }
  out[o] = 0;
}

uint32_t nowUnix() { return rtc_clock.getCurrentTime(); }
}  // namespace

CardputerMesh::CardputerMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables,
                             DeviceSettings& settings)
    : BaseChatMesh(radio, g_millis, rng, rtc, g_packets, tables), _settings(settings) {}

// ================================================================ startup / persistence

void CardputerMesh::begin() {
  BaseChatMesh::begin();
  loadIdentity();
  loadContacts();
  bootstrapRTCfromContacts();
  loadChannels();
  _store.begin(&_storage);
  // Channels are listed in Messages even before their first message
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (getChannel(i, ch) && ch.name[0]) _store.ensure(channelKey(ch.channel), ch.name);
  }
  _store.flush();
  _started = true;
  CPLOG_I("MESH", "mesh ready: %d contacts, %d conversations, %d unread", getNumContacts(), _store.count(),
                _store.unreadTotal());
}

void CardputerMesh::loadIdentity() {
  IdentityStore ids(LittleFS, "/identity");
  if (ids.load("_main", self_id)) {
    CPLOG_I("MESH", "identity loaded, key %02x%02x%02x%02x...", self_id.pub_key[0], self_id.pub_key[1],
                  self_id.pub_key[2], self_id.pub_key[3]);
    return;
  }
  // New device: random identity from radio noise (avoid the reserved 0x00 / 0xFF hashes)
  self_id = radio_new_identity();
  for (int n = 0; n < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF); n++) {
    self_id = radio_new_identity();
  }
  bool ok = ids.save("_main", self_id);
  CPLOG_I("MESH", "new identity created (%s), key %02x%02x%02x%02x...", ok ? "saved" : "SAVE FAILED",
                self_id.pub_key[0], self_id.pub_key[1], self_id.pub_key[2], self_id.pub_key[3]);
}

void CardputerMesh::loadContacts() {
  File f = LittleFS.open(CONTACTS_PATH, "r");
  if (!f) return;
  uint32_t magic = 0;
  if (f.read((uint8_t*)&magic, 4) != 4 || magic != CONTACTS_MAGIC) { f.close(); return; }
  ContactRecord r;
  int n = 0;
  while (f.read((uint8_t*)&r, sizeof(r)) == sizeof(r)) {
    ContactInfo c;
    memset(&c, 0, sizeof(c));
    c.id = mesh::Identity(r.pubKey);
    memcpy(c.name, r.name, sizeof(c.name));
    c.name[sizeof(c.name) - 1] = 0;
    c.type = r.type;
    c.flags = r.flags;
    c.out_path_len = r.outPathLen;
    memcpy(c.out_path, r.outPath, sizeof(c.out_path));
    c.last_advert_timestamp = r.lastAdvertTs;
    c.lastmod = r.lastmod;
    c.gps_lat = r.lat;
    c.gps_lon = r.lon;
    if (!addContact(c)) break;
    n++;
  }
  f.close();
  _orderDirty = true;
  CPLOG_I("MESH", "%d contacts loaded", n);
}

void CardputerMesh::saveContacts() {
  File f = LittleFS.open(CONTACTS_PATH, "w");
  if (!f) { CPLOG_E("MESH", "cannot save contacts"); return; }
  f.write((const uint8_t*)&CONTACTS_MAGIC, 4);
  ContactsIterator it = startContactsIterator();
  ContactInfo c;
  int n = 0;
  while (it.hasNext(this, c)) {
    ContactRecord r;
    memset(&r, 0, sizeof(r));
    memcpy(r.pubKey, c.id.pub_key, PUB_KEY_SIZE);
    memcpy(r.name, c.name, sizeof(r.name));
    r.type = c.type;
    r.flags = c.flags;
    r.outPathLen = c.out_path_len;
    memcpy(r.outPath, c.out_path, sizeof(r.outPath));
    r.lastAdvertTs = c.last_advert_timestamp;
    r.lastmod = c.lastmod;
    r.lat = c.gps_lat;
    r.lon = c.gps_lon;
    if (f.write((const uint8_t*)&r, sizeof(r)) != sizeof(r)) break;
    n++;
  }
  f.close();
  _contactsDirty = false;
  CPLOG_I("MESH", "%d contacts saved", n);
}

bool CardputerMesh::addChannelSecret(const char* name, const uint8_t* secret, int len) {
  char b64[48];
  base64(secret, len, b64);
  return addChannel(name, b64) != nullptr;
}

void CardputerMesh::loadChannels() {
  addChannel("Public", PUBLIC_PSK_B64);
  File f = LittleFS.open(CHANNELS_PATH, "r");
  if (!f) return;
  uint32_t magic = 0;
  if (f.read((uint8_t*)&magic, 4) == 4 && magic == CHANNELS_MAGIC) {
    ChannelRecord r;
    while (f.read((uint8_t*)&r, sizeof(r)) == sizeof(r)) {
      r.name[sizeof(r.name) - 1] = 0;
      if (r.len == 16 || r.len == 32) addChannelSecret(r.name, r.secret, r.len);
    }
  }
  f.close();
}

void CardputerMesh::saveChannels() {
  File f = LittleFS.open(CHANNELS_PATH, "w");
  if (!f) return;
  f.write((const uint8_t*)&CHANNELS_MAGIC, 4);
  for (int i = 1; i < MAX_GROUP_CHANNELS; i++) {   // slot 0 = Public, always re-created
    ChannelDetails ch;
    if (!getChannel(i, ch) || !ch.name[0]) continue;
    ChannelRecord r;
    memset(&r, 0, sizeof(r));
    memcpy(r.name, ch.name, sizeof(r.name));
    memcpy(r.secret, ch.channel.secret, sizeof(r.secret));
    r.len = 16;   // hashtag channels use 128-bit keys
    f.write((const uint8_t*)&r, sizeof(r));
  }
  f.close();
}

// ================================================================ keys and lookups

ConvKey CardputerMesh::directKey(const uint8_t* pubKey) {
  ConvKey k;
  k.kind = ConvKey::DIRECT;
  memcpy(k.id, pubKey, sizeof(k.id));
  return k;
}

ConvKey CardputerMesh::channelKey(const mesh::GroupChannel& ch) {
  ConvKey k;
  k.kind = ConvKey::CHANNEL;
  uint32_t a = fnv(ch.secret, sizeof(ch.secret)), b = fnv(ch.secret, sizeof(ch.secret), a);
  memcpy(k.id, &a, 4);
  memcpy(k.id + 4, &b, 2);
  return k;
}

ContactInfo* CardputerMesh::contactFor(const ConvKey& key) {
  if (key.kind != ConvKey::DIRECT) return nullptr;
  return lookupContactByPubKey(key.id, sizeof(key.id));
}

int CardputerMesh::channelIndexFor(const ConvKey& key) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (getChannel(i, ch) && ch.name[0] && channelKey(ch.channel) == key) return i;
  }
  return -1;
}

void CardputerMesh::toView(const ContactInfo& c, ContactView& out) {
  out = ContactView{};
  out.key = directKey(c.id.pub_key);
  memcpy(out.pubKey, c.id.pub_key, sizeof(out.pubKey));
  memcpy(out.name, c.name, sizeof(out.name));
  out.name[sizeof(out.name) - 1] = 0;
  out.type = c.type;
  out.favourite = c.isFav();
  out.hops = c.out_path_len == OUT_PATH_UNKNOWN ? -1 : (int8_t)(c.out_path_len & 63);
  out.lastHeard = c.lastmod;
  out.hasPos = c.gps_lat != 0 || c.gps_lon != 0;
  out.latE6 = c.gps_lat;
  out.lonE6 = c.gps_lon;
}

// ================================================================ contacts (UI)

void CardputerMesh::rebuildContactOrder() {
  _orderCount = 0;
  ContactInfo c;
  // Sort keys only (fav, lastmod): copying ContactInfo during the sort would be expensive
  static uint32_t keys[MAX_CONTACTS];
  for (int idx = MAX_ANON_CONTACTS; idx < getTotalContactSlots() && _orderCount < MAX_CONTACTS; idx++) {
    if (!getContactByIdx(idx, c) || c.type == 0) continue;
    _order[_orderCount] = idx;
    // favourites first (top bit), then most recent lastmod
    keys[_orderCount] = (c.isFav() ? 0x80000000u : 0) | ((c.lastmod >> 1) & 0x7FFFFFFFu);
    _orderCount++;
  }
  for (int i = 1; i < _orderCount; i++) {
    uint16_t o = _order[i];
    uint32_t k = keys[i];
    int j = i - 1;
    while (j >= 0 && keys[j] < k) { keys[j + 1] = keys[j]; _order[j + 1] = _order[j]; j--; }
    keys[j + 1] = k;
    _order[j + 1] = o;
  }
  _orderDirty = false;
}

bool CardputerMesh::contactAt(int i, ContactView& out) {
  if (_orderDirty) rebuildContactOrder();
  if (i < 0 || i >= _orderCount) return false;
  ContactInfo c;
  if (!getContactByIdx(_order[i], c)) return false;
  toView(c, out);
  return true;
}

bool CardputerMesh::findContact(const ConvKey& key, ContactView& out) {
  ContactInfo* c = contactFor(key);
  if (!c) return false;
  toView(*c, out);
  return true;
}

bool CardputerMesh::setFavourite(const ConvKey& key, bool fav) {
  ContactInfo* c = contactFor(key);
  if (!c) return false;
  if (fav) c->flags |= 0x01; else c->flags &= ~0x01;
  _orderDirty = true;
  saveContacts();   // explicit user action: save now
  return true;
}

bool CardputerMesh::removeContact(const ConvKey& key) {
  ContactInfo* c = contactFor(key);
  if (!c) return false;
  bool ok = BaseChatMesh::removeContact(*c);
  _orderDirty = true;
  saveContacts();
  return ok;
}

bool CardputerMesh::resetPath(const ConvKey& key) {
  ContactInfo* c = contactFor(key);
  if (!c) return false;
  resetPathTo(*c);
  saveContacts();
  return true;
}

bool CardputerMesh::sendAdvert(bool flood, bool withPosition) {
  if (!_started) return false;
  bool pos = withPosition && _gps && _gps->state().valid;
  mesh::Packet* pkt = pos ? createSelfAdvert(selfName(), _gps->state().lat, _gps->state().lon)
                          : createSelfAdvert(selfName());
  if (!pkt) return false;
  if (flood) sendFlood(pkt); else sendZeroHop(pkt);
  if (pos) CPLOG_I("MESH", "advert sent (%s) as '%s' at %.5f, %.5f", flood ? "flood" : "zero hop", selfName(),
                         _gps->state().lat, _gps->state().lon);
  else CPLOG_I("MESH", "advert sent (%s) as '%s'", flood ? "flood" : "zero hop", selfName());
  return true;
}

// ================================================================ channels (UI)

bool CardputerMesh::joinChannel(const char* name) {
  if (!name || !*name) return false;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (getChannel(i, ch) && strcmp(ch.name, name) == 0) return true;   // already joined
  }
  // Hashtag channel: key = first 16 bytes of SHA-256 of the name (with the '#')
  uint8_t secret[32];
  mesh::Utils::sha256(secret, 16, (const uint8_t*)name, strlen(name));
  if (!addChannelSecret(name, secret, 16)) return false;
  saveChannels();
  int idx = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (getChannel(i, ch) && strcmp(ch.name, name) == 0) idx = i;
  }
  ChannelDetails ch;
  if (idx >= 0 && getChannel(idx, ch)) _store.ensure(channelKey(ch.channel), name);
  CPLOG_I("MESH", "joined channel %s", name);
  return true;
}

bool CardputerMesh::leaveChannel(const ConvKey& key) {
  int idx = channelIndexFor(key);
  if (idx <= 0) return false;   // Public (slot 0) cannot be left
  ChannelDetails empty;
  memset(&empty, 0, sizeof(empty));
  setChannel(idx, empty);       // empty name = slot ignored by the mesh
  saveChannels();
  _store.remove(key);
  return true;
}

int CardputerMesh::maxTextLen(const ConvKey& to) {
  if (to.kind == ConvKey::CHANNEL) return MAX_TEXT_LEN - (int)strlen(selfName()) - 2;   // "name: " prefix
  return MAX_TEXT_LEN;
}

// ================================================================ sending

bool CardputerMesh::transmit(Pending& p, StoredMsg& m, ContactInfo& c) {
  uint32_t ack = 0, timeout = 0;
  int rc = sendMessage(c, p.ts, p.attempts, m.text, ack, timeout);
  if (rc == MSG_SEND_FAILED) return false;
  p.acks[p.attempts] = ack;
  p.attempts++;
  p.deadline = millis() + timeout + 1000;
  m.attempt = p.attempts;
  m.ackTag = ack;
  m.state = rc == MSG_SEND_SENT_DIRECT ? MSG_SENT_DIRECT : MSG_SENT_FLOOD;
  CPLOG_I("MESH", "msg to %s attempt %u (%s), ack %08x, timeout %u ms", c.name, p.attempts,
                rc == MSG_SEND_SENT_DIRECT ? "direct" : "flood", (unsigned)ack, (unsigned)timeout);
  return true;
}

bool CardputerMesh::sendText(const ConvKey& to, const char* text) {
  if (!_started || !text || !*text) return false;
  StoredMsg m;
  m.ts = nowUnix();
  m.outgoing = 1;
  utf8Copy(m.text, (size_t)maxTextLen(to) + 1, text);

  if (to.kind == ConvKey::CHANNEL) {
    int idx = channelIndexFor(to);
    ChannelDetails ch;
    if (idx < 0 || !getChannel(idx, ch)) return false;
    if (!sendGroupMessage(m.ts, ch.channel, selfName(), m.text, strlen(m.text))) return false;
    m.state = MSG_CHANNEL_SENT;
    return _store.append(to, ch.name, m, false) >= 0;
  }

  ContactInfo* c = contactFor(to);
  if (!c) return false;
  Pending* p = nullptr;
  for (auto& e : _pending) if (!e.used) { p = &e; break; }
  if (!p) {   // all slots busy: give up on the oldest
    p = &_pending[0];
    for (auto& e : _pending) if ((int32_t)(e.deadline - p->deadline) < 0) p = &e;
  }
  *p = Pending{};
  p->key = to;
  p->ts = m.ts;
  if (!transmit(*p, m, *c)) return false;
  int32_t seq = _store.append(to, c->name, m, false);
  if (seq < 0) return false;
  p->seq = seq;
  p->used = true;
  return true;
}

void CardputerMesh::checkPending() {
  uint32_t now = millis();
  for (auto& p : _pending) {
    if (!p.used || (int32_t)(now - p.deadline) < 0) continue;
    StoredMsg m;
    ContactInfo* c = contactFor(p.key);
    if (!c || !_store.read(p.key, p.seq, m)) { p.used = false; continue; }
    if (p.attempts < MAX_ATTEMPTS) {
      if (p.attempts == MAX_ATTEMPTS - 1) resetPathTo(*c);   // last try: flood, the path may be stale
      if (!transmit(p, m, *c)) p.deadline = now + 2000;     // radio busy: try again shortly
      _store.update(p.key, p.seq, m);
    } else {
      m.state = MSG_FAILED;
      _store.update(p.key, p.seq, m);
      pushEvent(MeshEvent::FAILED, p.key, c->name, m.text);
      CPLOG_I("MESH", "msg to %s FAILED after %u attempts", c->name, p.attempts);
      p.used = false;
    }
  }
}

ContactInfo* CardputerMesh::processAck(const uint8_t* data) {
  uint32_t ack;
  memcpy(&ack, data, 4);
  for (auto& p : _pending) {
    if (!p.used) continue;
    for (int i = 0; i < p.attempts; i++) {
      if (p.acks[i] != ack) continue;
      StoredMsg m;
      ContactInfo* c = contactFor(p.key);
      if (_store.read(p.key, p.seq, m)) {
        m.state = MSG_DELIVERED;
        _store.update(p.key, p.seq, m);
        pushEvent(MeshEvent::DELIVERED, p.key, c ? c->name : "", m.text);
      }
      CPLOG_I("MESH", "ACK %08x (%s)", (unsigned)ack, c ? c->name : "?");
      p.used = false;
      return c;
    }
  }
  return nullptr;
}

uint32_t CardputerMesh::calcFloodTimeoutMillisFor(uint32_t airtime) const {
  return 500 + (uint32_t)(16.0f * airtime);
}

uint32_t CardputerMesh::calcDirectTimeoutMillisFor(uint32_t airtime, uint8_t path_len) const {
  uint8_t hops = path_len & 63;
  return 500 + (uint32_t)((airtime * 6.0f + 250) * (hops + 1));
}

// ================================================================ receiving

bool CardputerMesh::isDuplicate(const uint8_t* prefix, uint32_t ts, const char* text) {
  // The sender retries with the same timestamp when our ACK got lost: show the message once.
  uint32_t key = fnv(prefix, 4, ts);
  uint32_t hash = fnv(text, strlen(text));
  for (auto& d : _dedup) if (d.key == key && d.hash == hash) return true;
  _dedup[_dedupPos] = {key, hash};
  _dedupPos = (_dedupPos + 1) % DEDUP;
  return false;
}

void CardputerMesh::storeIncoming(const ContactInfo& from, mesh::Packet* pkt, uint32_t senderTs, const char* text) {
  if (isDuplicate(from.id.pub_key, senderTs, text)) return;
  ConvKey k = directKey(from.id.pub_key);
  StoredMsg m;
  m.ts = nowUnix();
  m.state = MSG_RECEIVED;
  m.snrX4 = (int8_t)(pkt->getSNR() * 4);
  if (pkt->isRouteFlood()) m.hops = pkt->getPathHashCount();
  else m.hops = from.out_path_len == OUT_PATH_UNKNOWN ? 0xFF : (from.out_path_len & 63);
  utf8Copy(m.text, sizeof(m.text), text);
  _store.append(k, from.name, m, true);
  pushEvent(MeshEvent::MESSAGE, k, from.name, m.text);
  CPLOG_I("MESH", "msg from %s (%s, snr %.1f)", from.name, pkt->isRouteFlood() ? "flood" : "direct",
                pkt->getSNR());
}

void CardputerMesh::onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t ts, const char* text) {
  storeIncoming(contact, pkt, ts, text);
}

void CardputerMesh::onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t ts,
                                        const uint8_t*, const char* text) {
  storeIncoming(contact, pkt, ts, text);   // room server posts
}

void CardputerMesh::onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t ts,
                                         const char* text) {
  int idx = findChannelIdx(channel);
  ChannelDetails ch;
  if (idx < 0 || !getChannel(idx, ch)) return;
  ConvKey k = channelKey(ch.channel);
  StoredMsg m;
  m.ts = nowUnix();
  m.state = MSG_RECEIVED;
  m.snrX4 = (int8_t)(pkt->getSNR() * 4);
  m.hops = pkt->isRouteFlood() ? pkt->getPathHashCount() : 0;
  // "sender: message"
  const char* sep = strstr(text, ": ");
  if (sep && sep - text < (int)sizeof(m.sender)) {
    memcpy(m.sender, text, sep - text);
    m.sender[sep - text] = 0;
    utf8Copy(m.text, sizeof(m.text), sep + 2);
  } else {
    utf8Copy(m.text, sizeof(m.text), text);
  }
  if (strcmp(m.sender, selfName()) == 0) return;   // our own message echoed back
  uint8_t prefix[4];
  memcpy(prefix, k.id, 4);
  if (isDuplicate(prefix, ts, text)) return;
  _store.append(k, ch.name, m, true);
  char preview[64];
  snprintf(preview, sizeof(preview), "%s: %s", m.sender, m.text);
  pushEvent(MeshEvent::MESSAGE, k, ch.name, preview);
  CPLOG_I("MESH", "channel %s msg (%u hops, snr %.1f)", ch.name, m.hops, pkt->getSNR());
}

void CardputerMesh::onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp,
                                 const uint8_t* app_data, size_t app_data_len) {
  // Remember the SNR of neighbours for the home screen (BaseChatMesh does not keep it)
  int slot = 0;
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (memcmp(_nbs[i].prefix, id.pub_key, 6) == 0) { slot = i; break; }
    if ((int32_t)(_nbs[i].heardMs - _nbs[slot].heardMs) < 0) slot = i;
  }
  memcpy(_nbs[slot].prefix, id.pub_key, 6);
  _nbs[slot].snrX4 = (int8_t)(packet->getSNR() * 4);
  _nbs[slot].heardMs = millis() | 1;
  BaseChatMesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len);
}

void CardputerMesh::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t, const uint8_t*) {
  _orderDirty = true;
  if (!_contactsDirty) _contactsDirtySince = millis();
  _contactsDirty = true;
  _store.rename(directKey(contact.id.pub_key), contact.name);   // keep conversation names current
  if (is_new) {
    pushEvent(MeshEvent::CONTACT_NEW, directKey(contact.id.pub_key), contact.name, "");
    CPLOG_I("MESH", "new contact %s (type %u)", contact.name, contact.type);
  }
}

void CardputerMesh::onContactPathUpdated(const ContactInfo&) {
  if (!_contactsDirty) _contactsDirtySince = millis();
  _contactsDirty = true;
}

// ================================================================ events, loop, stats

void CardputerMesh::pushEvent(MeshEvent::Type t, const ConvKey& k, const char* title, const char* text) {
  uint8_t pos = (_evHead + _evCount) % MAX_EVENTS;
  if (_evCount == MAX_EVENTS) _evHead = (_evHead + 1) % MAX_EVENTS;   // drop the oldest
  else _evCount++;
  MeshEvent& e = _events[pos];
  e.type = t;
  e.key = k;
  utf8Copy(e.title, sizeof(e.title), title);
  utf8Copy(e.text, sizeof(e.text), text);
}

bool CardputerMesh::pollEvent(MeshEvent& out) {
  if (!_evCount) return false;
  out = _events[_evHead];
  _evHead = (_evHead + 1) % MAX_EVENTS;
  _evCount--;
  return true;
}

void CardputerMesh::loop() {
  if (!_started) return;
  BaseChatMesh::loop();
  checkPending();
  uint32_t now = millis();
  // Deferred writes: group bursts of adverts / messages into one flash write
  if (_contactsDirty && now - _contactsDirtySince > 10000) saveContacts();
  if (_store.dirty() && now - _lastFlush > 3000) {
    _store.flush();
    _lastFlush = now;
  }
}

int CardputerMesh::neighbours(NeighbourInfo* out, int max) {
  int idx[MAX_NEIGHBOURS], n = 0;
  for (int i = 0; i < MAX_NEIGHBOURS; i++) if (_nbs[i].heardMs) idx[n++] = i;
  for (int i = 1; i < n; i++) {   // most recent first
    int v = idx[i], j = i - 1;
    while (j >= 0 && (int32_t)(_nbs[idx[j]].heardMs - _nbs[v].heardMs) < 0) { idx[j + 1] = idx[j]; j--; }
    idx[j + 1] = v;
  }
  int count = 0;
  uint32_t now = millis();
  for (int i = 0; i < n && count < max; i++) {
    const Neighbour& nb = _nbs[idx[i]];
    ContactInfo* c = lookupContactByPubKey(nb.prefix, 6);
    if (!c) continue;
    utf8Copy(out[count].name, sizeof(out[count].name), c->name);
    out[count].snr = nb.snrX4 / 4.0f;
    out[count].lastHeardSecs = (now - nb.heardMs) / 1000;
    count++;
  }
  return count;
}

RadioStats CardputerMesh::radioStats() {
  RadioStats r;
  const UiSettings& s = _settings.get();
  const RadioPreset& p = RADIO_PRESETS[s.radioPreset < RADIO_PRESET_COUNT ? s.radioPreset : 0];
  r.freqMHz = p.freqMHz;
  r.bandwidthKHz = p.bwKHz;
  r.spreadingFactor = p.sf;
  r.txPowerDbm = s.txPowerDbm;
  r.rxPackets = radio_driver.getPacketsRecv();
  r.txPackets = radio_driver.getPacketsSent();
  r.rxErrors = radio_driver.getPacketsRecvErrors();
  r.lastRssi = radio_driver.getLastRSSI();
  r.lastSnr = radio_driver.getLastSNR();
  return r;
}

#endif  // !CPADV_DEMO
