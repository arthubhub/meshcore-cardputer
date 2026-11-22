#include "ArchivingChatMesh.h"
#include <helpers/AdvertDataHelpers.h>
#include <string.h>

namespace {
void recordFrom(const ContactInfo& c, NodeRecord& r) {
  r = NodeRecord();
  memcpy(r.pubKey, c.id.pub_key, PUB_KEY_SIZE);
  memcpy(r.name, c.name, sizeof(r.name));
  r.name[sizeof(r.name) - 1] = 0;
  r.type = c.type;
  r.lastHeard = c.lastmod;
  r.lastAdvertTs = c.last_advert_timestamp;
  if (c.gps_lat || c.gps_lon) {
    r.flags |= NodeRecord::HasPos;
    r.latE6 = c.gps_lat;
    r.lonE6 = c.gps_lon;
    r.posSource = 1;   // PositionSource::Advert: its time is not known here
  }
}
}  // namespace

// ---------------------------------------------------------------- adverts

void ArchivingChatMesh::onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp,
                                     const uint8_t* app_data, size_t app_data_len) {
  BaseChatMesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len);
  if (!_book) return;
  AdvertDataParser parser(app_data, app_data_len);
  if (!(parser.isValid() && parser.hasName())) return;   // BaseChatMesh refused it too
  NodeRecord r;
  memcpy(r.pubKey, id.pub_key, PUB_KEY_SIZE);
  StrHelper::strncpy(r.name, parser.getName(), sizeof(r.name));
  r.type = parser.getType();
  r.lastHeard = getRTCClock()->getCurrentTime();
  r.lastAdvertTs = timestamp;
  r.hops = packet->isRouteFlood() ? (uint8_t)packet->getPathHashCount() : 0;
  r.snrX4 = (int8_t)(packet->getSNR() * 4);
  r.rssi = NodeRecord::clampRssi(lastRssi());
  if (parser.hasLatLon()) {
    r.flags |= NodeRecord::HasPos;
    r.latE6 = parser.getIntLat();
    r.lonE6 = parser.getIntLon();
    r.posTime = timestamp;
    r.posSource = 1;   // PositionSource::Advert
  }
  const uint32_t now = nowMs();
  _book->heard(r, now);
  _ast.adverts++;
  // The table's view of this node: in it (with its favourite flag) or not
  if (ContactInfo* c = lookupContactByPubKey(id.pub_key, PUB_KEY_SIZE)) {
    const uint8_t fav = c->isFav() ? NodeRecord::Favourite : 0;
    _book->markFlags(id.pub_key, (uint8_t)(NodeRecord::InContacts | fav), (uint8_t)(fav ? 0 : NodeRecord::Favourite), now);
  }
}

void ArchivingChatMesh::archiveContact(const ContactInfo& c, bool inTable) {
  if (!_book) return;
  NodeRecord r;
  recordFrom(c, r);
  const uint32_t now = nowMs();
  _book->heard(r, now);
  const uint8_t fav = c.isFav() ? NodeRecord::Favourite : 0;
  if (inTable) _book->markFlags(c.id.pub_key, (uint8_t)(NodeRecord::InContacts | fav), fav ? 0 : NodeRecord::Favourite, now);
  else _book->markFlags(c.id.pub_key, 0, NodeRecord::InContacts, now);
}

void ArchivingChatMesh::archiveLeaving(const uint8_t* pub_key, bool keep) {
  if (!_book) return;
  if (const ContactInfo* c = lookupContactByPubKey(pub_key, PUB_KEY_SIZE)) {
    NodeRecord r;
    recordFrom(*c, r);
    _book->heard(r, nowMs());          // what the table knew, in case the archive never heard it
  }
  _book->markFlags(pub_key, keep ? NodeRecord::Keep : 0, NodeRecord::InContacts, nowMs());
}

// ---------------------------------------------------------------- senders outside the table

int ArchivingChatMesh::searchPeersByHash(const uint8_t* hash) {
  _tableMatches = BaseChatMesh::searchPeersByHash(hash);
  _hash = hash[0];
  _loaded = false;
  _nc = 0;
  if (!_book) return _tableMatches;
  // The archive is read only if every candidate of the table fails (getPeerSharedSecret): until then these are
  // placeholders, which MeshCore never reaches when a contact of the table decrypts the packet.
  int extra = archiveCandidates();
  if (extra > MAX_CANDIDATES) extra = MAX_CANDIDATES;
  if (extra < 0) extra = 0;
  if (_tableMatches + extra > MAX_SEARCH_RESULTS) extra = MAX_SEARCH_RESULTS - _tableMatches;
  return _tableMatches + extra;
}

bool ArchivingChatMesh::loadCandidates() {
  if (_loaded) return _nc > 0;
  _loaded = true;
  _nc = 0;
  _ast.lookups++;
  NodeRecord found[MAX_CANDIDATES];
  // Types that send to us: people and rooms (messages), repeaters and sensors (answers to our requests)
  const int n = _book->findByHash(&_hash, 1, NodeArchive::ALL_TYPES, found, MAX_CANDIDATES);
  const int limit = archiveCandidates() < MAX_CANDIDATES ? archiveCandidates() : MAX_CANDIDATES;
  for (int i = 0; i < n && i < MAX_CANDIDATES && _nc < limit; i++) {
    if (lookupContactByPubKey(found[i].pubKey, PUB_KEY_SIZE)) continue;   // tried already, as a contact of the table
    _cand[_nc++] = found[i];
  }
  // The most recently heard first: the likeliest sender
  for (int i = 1; i < _nc; i++) {
    for (int j = i; j > 0 && _cand[j].lastHeard > _cand[j - 1].lastHeard; j--) {
      NodeRecord t = _cand[j];
      _cand[j] = _cand[j - 1];
      _cand[j - 1] = t;
    }
  }
  return _nc > 0;
}

bool ArchivingChatMesh::takeEcdh() {
  const uint32_t now = nowMs();
  if (now - _ecdhWindowMs >= 60000) {
    _ecdhWindowMs = now;
    _ecdhInWindow = 0;
  }
  if (_ecdhInWindow >= ECDH_PER_MINUTE) {
    _ast.ecdhRefused++;
    return false;
  }
  _ecdhInWindow++;
  _ast.ecdh++;
  return true;
}

void ArchivingChatMesh::getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) {
  if (peer_idx < _tableMatches || !_book) {
    BaseChatMesh::getPeerSharedSecret(dest_secret, peer_idx);
    return;
  }
  const int c = peer_idx - _tableMatches;
  if (!loadCandidates() || c >= _nc || !takeEcdh()) {
    memset(dest_secret, 0, PUB_KEY_SIZE);   // no candidate there: a secret that no packet's MAC matches
    return;
  }
  self_id.calcSharedSecret(dest_secret, _cand[c].pubKey);
}

ContactInfo* ArchivingChatMesh::promote(const NodeRecord& r) {
  if (ContactInfo* c = lookupContactByPubKey(r.pubKey, PUB_KEY_SIZE)) return c;
  ContactInfo ci;
  memset(&ci, 0, sizeof(ci));
  ci.id = mesh::Identity(r.pubKey);
  memcpy(ci.name, r.name, sizeof(ci.name));
  ci.name[sizeof(ci.name) - 1] = 0;
  ci.type = r.type ? r.type : ADV_TYPE_CHAT;
  ci.flags = r.has(NodeRecord::Favourite) ? 0x01 : 0;
  ci.out_path_len = OUT_PATH_UNKNOWN;   // the route is learnt again (flood, then path return)
  ci.last_advert_timestamp = r.lastAdvertTs;   // MeshCore's replay check keeps working
  ci.lastmod = getRTCClock()->getCurrentTime();
  if (r.hasPos()) {
    ci.gps_lat = r.latE6;
    ci.gps_lon = r.lonE6;
  }
  if (!addContact(ci)) return nullptr;      // full of favourites (onContactsFull is MeshCore's to call on adverts)
  ContactInfo* c = lookupContactByPubKey(r.pubKey, PUB_KEY_SIZE);
  if (!c) return nullptr;
  _ast.broughtBack++;
  _book->markFlags(r.pubKey, NodeRecord::InContacts, 0, nowMs());
  onBroughtBack(*c);
  return c;
}

ContactInfo* ArchivingChatMesh::bringBack(const uint8_t* key, int keyLen) {
  if (ContactInfo* c = lookupContactByPubKey(key, keyLen)) return c;
  if (!_book || keyLen < 1) return nullptr;
  NodeRecord found[2];
  const int n = _book->findByHash(key, keyLen, NodeArchive::ALL_TYPES, found, 2);
  if (n != 1) return nullptr;              // unknown, or a prefix several nodes share
  return promote(found[0]);
}

int ArchivingChatMesh::tableIndexOf(const uint8_t* pubKey, const uint8_t* hash) const {
  // BaseChatMesh lists the matches of an ID in the order of its table: the same walk gives this node's index
  int k = 0;
  ContactInfo c;
  for (int i = 0; i < getTotalContactSlots(); i++) {
    if (!const_cast<ArchivingChatMesh*>(this)->getContactByIdx(i, c)) break;
    if (!c.id.isHashMatch(hash)) continue;
    if (memcmp(c.id.pub_key, pubKey, PUB_KEY_SIZE) == 0) return k;
    k++;
  }
  return -1;
}

void ArchivingChatMesh::onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret,
                                       uint8_t* data, size_t len) {
  if (sender_idx < _tableMatches || !_book) {
    BaseChatMesh::onPeerDataRecv(packet, type, sender_idx, secret, data, len);
    return;
  }
  const int c = sender_idx - _tableMatches;
  if (c >= _nc) return;
  const NodeRecord sender = _cand[c];
  if (!promote(sender)) return;
  const uint8_t hash = _hash;
  BaseChatMesh::searchPeersByHash(&hash);   // the table changed: MeshCore's own list of matches again
  const int k = tableIndexOf(sender.pubKey, &hash);
  if (k >= 0) BaseChatMesh::onPeerDataRecv(packet, type, k, secret, data, len);
}

bool ArchivingChatMesh::onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path,
                                       uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  if (sender_idx < _tableMatches || !_book) {
    return BaseChatMesh::onPeerPathRecv(packet, sender_idx, secret, path, path_len, extra_type, extra, extra_len);
  }
  const int c = sender_idx - _tableMatches;
  if (c >= _nc) return false;
  const NodeRecord sender = _cand[c];
  if (!promote(sender)) return false;
  const uint8_t hash = _hash;
  BaseChatMesh::searchPeersByHash(&hash);
  const int k = tableIndexOf(sender.pubKey, &hash);
  if (k < 0) return false;
  return BaseChatMesh::onPeerPathRecv(packet, k, secret, path, path_len, extra_type, extra, extra_len);
}
