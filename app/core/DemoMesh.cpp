#include "DemoMesh.h"
#include "I18n.h"
#include "RadioPresets.h"
#include "drivers/GeoText.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <ctype.h>
#include <initializer_list>

static void defaultSink(const char* line) { printf("[SIM] %s\n", line); }
void (*DemoMesh::logSink)(const char* line) = defaultSink;

void DemoMesh::logf(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  logSink(buf);
}

// ------------------------------------------------------------------ DemoMesh

static ConvKey keyFromName(uint8_t kind, const std::string& name) {
  // Stable fake "public key prefix": FNV-1a of the name
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : name) { h ^= c; h *= 1099511628211ull; }
  ConvKey k;
  k.kind = kind;
  for (int i = 0; i < 6; i++) k.id[i] = (h >> (i * 8)) & 0xFF;
  return k;
}

DemoMesh::DemoMesh(SystemService& sys, SettingsService& settings, Storage& storage)
    : _sys(sys), _settings(settings), _storage(storage) {}

void DemoMesh::begin() {
  _store.begin(&_storage);
  joinChannel("Public");
  _store.flush();
}

void DemoMesh::reboot() {
  _store.flush();
  _pending.clear();
  _events.clear();
  _store.begin(&_storage);
  logf("reboot: %d conversation(s) reloaded, %d unread", _store.count(), _store.unreadTotal());
}

DemoMesh::Contact* DemoMesh::findMut(const ConvKey& k) {
  for (auto& c : _contacts) if (c.v.key == k) return &c;
  return nullptr;
}

ConvKey DemoMesh::keyByName(const std::string& name) {
  for (auto& ch : _channels) if (ch.name == name) return ch.key;
  for (auto& c : _contacts) if (name == c.v.name) return c.v.key;
  for (int i = 0; i < _store.count(); i++) if (name == _store.at(i)->name) return _store.at(i)->key;
  return ConvKey();
}

void DemoMesh::sortContacts() {
  std::stable_sort(_contacts.begin(), _contacts.end(), [](const Contact& a, const Contact& b) {
    if (a.v.favourite != b.v.favourite) return a.v.favourite;
    return a.v.lastHeard > b.v.lastHeard;
  });
}

void DemoMesh::push(MeshEvent::Type t, const ConvKey& k, const std::string& title, const std::string& text) {
  MeshEvent e;
  e.type = t;
  e.key = k;
  utf8Copy(e.title, sizeof(e.title), title.c_str());
  utf8Copy(e.text, sizeof(e.text), text.c_str());
  _events.push_back(e);
}

void DemoMesh::addContact(const std::string& name, int hops, bool fav, uint8_t type, bool hasPos, double lat,
                         double lon) {
  ConvKey k = keyFromName(ConvKey::DIRECT, name);
  Contact* c = findMut(k);
  if (!c) {
    if (!makeRoom()) return;
    _contacts.push_back(Contact());
    c = &_contacts.back();
  }
  c->v = ContactView{};
  c->v.key = k;
  memcpy(c->v.pubKey, k.id, 6);
  utf8Copy(c->v.name, sizeof(c->v.name), name.c_str());
  c->v.type = type;
  c->v.favourite = fav;
  c->v.hops = hops;
  c->v.lastHeard = nowUnix();
  c->v.hasPos = hasPos;
  c->v.latE6 = (int32_t)(lat * 1e6);
  c->v.lonE6 = (int32_t)(lon * 1e6);
  c->v.posTime = hasPos ? nowUnix() : 0;
  c->v.posSource = (uint8_t)(hasPos ? PositionSource::Advert : PositionSource::None);
  uint8_t path[64];
  for (size_t i = 0; i < sizeof(path); i++) path[i] = (uint8_t)(k.id[i % 6] ^ (i * 37));
  if (hops >= 0) c->v.route.set(PATH_PLANNED, (uint8_t)hops, path);
  c->heardAtMs = _sys.millis();
  c->snr = 7.25f;
  sortContacts();
}

bool DemoMesh::makeRoom() {
  if ((int)_contacts.size() < capacity) return true;
  Contact* oldest = nullptr;
  for (auto& c : _contacts) {
    if (!c.v.favourite && (!oldest || c.heardAtMs < oldest->heardAtMs)) oldest = &c;
  }
  char text[16];
  snprintf(text, sizeof(text), "%d/%d", (int)_contacts.size(), capacity);
  if (!oldest) {
    push(MeshEvent::CONTACTS_BLOCKED, ConvKey(), "", text);
    logf("contact list full of favourites: a new contact was refused");
    return false;
  }
  logf("contact list full: %s replaced by a new contact", oldest->v.name);
  _contacts.erase(_contacts.begin() + (oldest - &_contacts[0]));
  return true;
}

void DemoMesh::hear(const std::string& name, float snr) {
  ConvKey k = keyFromName(ConvKey::DIRECT, name);
  Contact* c = findMut(k);
  if (!c) {
    addContact(name, 1, false, CONTACT_CHAT, false, 0, 0);
    c = findMut(k);
    if (!c) return;   // refused: the list is full of favourites
    push(MeshEvent::CONTACT_NEW, k, name, "");
  }
  c->snr = snr;
  c->heardAtMs = _sys.millis();
  c->v.lastHeard = nowUnix();
  recordRx(wire::PL_ADVERT, k, std::max<int>(0, c->v.hops), snr);
  sortContacts();
}

void DemoMesh::setContactKey(const std::string& name, const std::vector<uint8_t>& keyStart) {
  Contact* c = findMut(keyFromName(ConvKey::DIRECT, name));
  if (!c) return;
  memset(c->v.pubKey, 0, sizeof(c->v.pubKey));
  memcpy(c->v.pubKey, keyStart.data(), keyStart.size() < sizeof(c->v.pubKey) ? keyStart.size() : sizeof(c->v.pubKey));
}

MsgRoute DemoMesh::repeaterRoute(int hops) const {
  uint8_t path[64];
  int n = 0;
  for (auto& c : _contacts) {
    if (c.v.type == CONTACT_REPEATER && n < hops) path[n++] = c.v.pubKey[0];
  }
  MsgRoute r;
  r.set(PATH_TAKEN, (uint8_t)n, path);
  return r;
}

void DemoMesh::receive(const std::string& sender, const std::string& text, const MsgRoute* route) {
  hear(sender, 6.5f);
  ConvKey k = keyFromName(ConvKey::DIRECT, sender);
  Contact* c = findMut(k);
  StoredMsg m;
  m.ts = nowUnix();
  m.state = MSG_RECEIVED;
  m.snrX4 = 26;
  m.hops = c ? std::max<int>(0, c->v.hops) : 1;
  m.rssi = -96;
  m.routeType = wire::ROUTE_FLOOD;
  m.route = route ? *route : repeaterRoute(m.hops);
  if (route) m.hops = (uint8_t)route->view().hops();
  utf8Copy(m.text, sizeof(m.text), text.c_str());
  _store.append(k, sender.c_str(), m, true);
  _rx++;
  recordRx(wire::PL_TXT, k, m.hops, m.snrX4 / 4.0f);
  double lat, lon;
  if (c && geotext::find(m.text, lat, lon)) {   // a direct message is authenticated (MESSAGING-11)
    ContactPosition pos;
    pos.latE6 = c->v.latE6;
    pos.lonE6 = c->v.lonE6;
    pos.time = c->v.posTime;
    pos.source = (PositionSource)c->v.posSource;
    bool taken = pos.offer((int32_t)lround(lat * 1e6), (int32_t)lround(lon * 1e6), nowUnix(), PositionSource::Message);
    if (taken) {
      c->v.latE6 = pos.latE6;
      c->v.lonE6 = pos.lonE6;
      c->v.posTime = pos.time;
      c->v.posSource = (uint8_t)pos.source;
      c->v.hasPos = true;
    }
    logf("position from %s in a message: %.5f, %.5f (%s)", sender.c_str(), lat, lon, taken ? "updated" : "older, ignored");
  }
  push(MeshEvent::MESSAGE, k, sender, text);
  logf("rx %s: %s", sender.c_str(), text.c_str());
}

void DemoMesh::receiveChannel(const std::string& channel, const std::string& sender, const std::string& text,
                              const MsgRoute* route) {
  ConvKey k = keyByName(channel);
  if (!k.valid() || k.kind != ConvKey::CHANNEL) {
    logf("rx on unknown channel %s ignored (not joined)", channel.c_str());
    return;
  }
  StoredMsg m;
  m.ts = nowUnix();
  m.state = MSG_RECEIVED;
  m.snrX4 = 18;
  m.hops = 2;
  m.rssi = -101;
  m.routeType = wire::ROUTE_FLOOD;
  m.route = route ? *route : repeaterRoute(m.hops);
  if (route) m.hops = (uint8_t)route->view().hops();
  utf8Copy(m.sender, sizeof(m.sender), sender.c_str());
  utf8Copy(m.text, sizeof(m.text), text.c_str());
  _store.append(k, channel.c_str(), m, true);
  _rx++;
  recordRx(wire::PL_GRP_TXT, k, m.hops, m.snrX4 / 4.0f);
  push(MeshEvent::MESSAGE, k, channel, sender + ": " + text);
  logf("rx #%s <%s>: %s", channel.c_str(), sender.c_str(), text.c_str());
}

int DemoMesh::maxTextLen(const ConvKey& to) {
  if (to.kind == ConvKey::CHANNEL) return MSG_TEXT_MAX - (int)strlen(selfName()) - 2;
  return MSG_TEXT_MAX;
}

void DemoMesh::transmit(Pending& p, StoredMsg& m) {
  m.attempt++;
  Contact* c = findMut(p.key);
  m.state = (c && c->v.hops >= 0) ? MSG_SENT_DIRECT : MSG_SENT_FLOOD;
  if (m.state == MSG_SENT_DIRECT) m.route = c->v.route;
  else m.route = MsgRoute();
  p.deadline = _sys.millis() + 4000;
  p.ackAt = netAcks ? _sys.millis() + netDelayMs : 0;
  sentPackets++;
  recordTx(wire::PL_TXT, p.key, m.state == MSG_SENT_FLOOD);
  logf("tx attempt %u to %s (%s)", m.attempt, c ? c->v.name : "?",
         m.state == MSG_SENT_DIRECT ? "direct" : "flood");
}

bool DemoMesh::sendText(const ConvKey& to, const char* text) {
  if (radioBusy) return false;
  StoredMsg m;
  m.ts = nowUnix();
  m.outgoing = 1;
  utf8Copy(m.text, (size_t)maxTextLen(to) + 1, text);
  const ConvMeta* meta = _store.find(to);
  std::string name = meta ? meta->name : "";
  if (to.kind == ConvKey::CHANNEL) {
    m.state = MSG_CHANNEL_SENT;
    sentPackets++;
    recordTx(wire::PL_GRP_TXT, to, true);
    if (_bot && rnd() % 2 == 0) scheduleReply(to, m.text);   // someone answers on the channel
    return _store.append(to, name.c_str(), m, false) >= 0;
  }
  Contact* c = findMut(to);
  if (!c) return false;
  Pending p{to, 0, 0, 0};
  transmit(p, m);
  int32_t seq = _store.append(to, c->v.name, m, false);
  if (seq < 0) return false;
  p.seq = seq;
  _pending.push_back(p);
  return true;
}

void DemoMesh::addNearby(const std::string& name, float snrThere, float snrHere) {
  _nearby.push_back(Nearby{name, snrThere, snrHere});
}

void DemoMesh::clearNearby() {
  _nearby.clear();
  _answers.clear();
}

// ------------------------------------------------------------------ packet log, airtime

// Real wire fields go through the real decoder of PacketLog
void DemoMesh::record(uint8_t route, uint8_t payload, const std::vector<uint8_t>& body, int hops, int hashBytes, float snr,
                      bool tx, bool scoped, bool ourRegion) {
  uint8_t path[64];
  for (size_t i = 0; i < sizeof(path); i++) path[i] = (uint8_t)(0xA1 + i * 29);
  PacketLog::Frame f;
  f.header = wire::header(scoped ? wire::ROUTE_TRANSPORT_FLOOD : route, payload);
  f.pathLen = (uint8_t)(((hashBytes - 1) << 6) | (hops & 63));
  f.path = path;
  f.payload = body.data();
  f.payloadLen = body.size();
  f.transportCode = scoped ? 0x5A17 : 0;
  f.rawLen = 2 + hops * hashBytes + (int)body.size() + 40;
  f.snr = snr;
  f.rssi = tx ? 0 : -110 + snr;
  f.ms = _sys.millis();
  f.unixTime = nowUnix();
  f.tx = tx;
  f.ourRegion = ourRegion;
  _packets.add(f);
}

void DemoMesh::recordTx(uint8_t payload, const ConvKey& key, bool flood) {
  const UiSettings& s = _settings.get();
  std::vector<uint8_t> body;
  if (payload == wire::PL_ADVERT) body.assign(key.id, key.id + sizeof(key.id));
  else if (payload == wire::PL_GRP_TXT) body = {key.id[0]};
  else body = {key.id[0], 0x42};
  record(flood ? wire::ROUTE_FLOOD : wire::ROUTE_DIRECT, payload, body, 0, flood ? floodopt::validPathBytes(s.pathHashBytes) : 1, 0, true,
         flood && s.region[0], flood && s.region[0]);
  addAirtime(600);
}

void DemoMesh::recordRx(uint8_t payload, const ConvKey& key, int hops, float snr) {
  std::vector<uint8_t> body;
  if (payload == wire::PL_ADVERT) body.assign(key.id, key.id + sizeof(key.id));
  else if (payload == wire::PL_GRP_TXT) body = {key.id[0]};
  else body = {0x42, key.id[0]};
  record(wire::ROUTE_FLOOD, payload, body, hops, 1, snr, false, false, false);
}

void DemoMesh::hearPacket(const std::string& type, const std::string& from, int hops, float snr, const std::string& region) {
  ConvKey k = keyFromName(ConvKey::DIRECT, from);
  for (uint8_t t = 0; t < 16; t++) {
    if (type != PacketLog::typeName(t)) continue;
    std::vector<uint8_t> body;
    if (t == wire::PL_ADVERT) body.assign(k.id, k.id + sizeof(k.id));
    else if (t == wire::PL_GRP_TXT) body = {k.id[0]};
    else body = {0x42, k.id[0]};
    bool scoped = !region.empty();
    bool ours = scoped && region == _settings.get().region;
    record(wire::ROUTE_FLOOD, t, body, hops, 1, snr, false, scoped, ours);
    return;
  }
  logf("unknown packet type %s", type.c_str());
}

void DemoMesh::addAirtime(uint32_t ms) {
  _airTotalMs += ms;
  _airtime.update(_airTotalMs, _sys.millis());
}

bool DemoMesh::startDiscovery() {
  uint32_t now = _sys.millis();
  if (!_discovery.canStart(now)) return false;
  _discovery.start(rnd(), now);
  sentPackets++;
  uint8_t req[NodeDiscovery::REQUEST_LEN];
  _discovery.buildRequest(req);
  std::vector<Nearby> who = _nearby;
  if (_bot) {   // demo firmware: its repeater contacts answer
    for (auto& c : _contacts) {
      if (c.v.type == CONTACT_REPEATER) who.push_back(Nearby{c.v.name, -2.5f - (rnd() % 20) / 2.0f, -4.0f - (rnd() % 20) / 2.0f});
    }
  }
  uint32_t delay = 1000;
  for (auto& n : who) {
    ConvKey k = keyFromName(ConvKey::DIRECT, n.name);
    Answer a;
    a.frame.resize(NodeDiscovery::MIN_RESPONSE_LEN, 0);
    a.frame[0] = 0x90 | CONTACT_REPEATER;            // CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER
    a.frame[1] = (uint8_t)(int8_t)(n.snrThere * 4);
    memcpy(&a.frame[2], &req[2], 4);                 // tag of the request
    memcpy(&a.frame[6], k.id, sizeof(k.id));
    a.snrHere = n.snrHere;
    a.at = now + delay;
    delay += 900;
    _answers.push_back(a);
  }
  return true;
}

void DemoMesh::tick() {
  uint32_t now = _sys.millis();
  if (_bot) botTick();
  const ContactCapacity::Alert full = _capacityAlert.poll((int)_contacts.size(), capacity);
  if (full != ContactCapacity::Alert::None) {
    char text[16];
    snprintf(text, sizeof(text), "%d/%d", (int)_contacts.size(), capacity);
    push(full == ContactCapacity::Alert::Full ? MeshEvent::CONTACTS_REPLACING : MeshEvent::CONTACTS_NEAR_FULL, ConvKey(), "", text);
  }
  _airtime.update(_airTotalMs, now);
  AirtimeMeter::Alert alert = _airtime.pollAlert(radioStats().txAirLimitPercent);
  if (alert != AirtimeMeter::Alert::None) {
    char text[32];
    snprintf(text, sizeof(text), "%.1f %% / %g %%", _airtime.percent(), radioStats().txAirLimitPercent);
    push(MeshEvent::AIRTIME, ConvKey(), "", text);
  }
  for (size_t i = 0; i < _answers.size();) {
    if ((int32_t)(now - _answers[i].at) >= 0) {
      _discovery.accept(_answers[i].frame.data(), _answers[i].frame.size(), _answers[i].snrHere, -100 + _answers[i].snrHere, now);
      _answers.erase(_answers.begin() + i);
    } else {
      i++;
    }
  }
  for (size_t i = 0; i < _pending.size();) {
    Pending& p = _pending[i];
    StoredMsg m;
    if (!_store.read(p.key, p.seq, m)) { _pending.erase(_pending.begin() + i); continue; }
    const ConvMeta* meta = _store.find(p.key);
    std::string name = meta ? meta->name : "?";
    if (p.ackAt && (int32_t)(now - p.ackAt) >= 0) {
      m.state = MSG_DELIVERED;
      _store.update(p.key, p.seq, m);
      recordRx(wire::PL_ACK, p.key, 1, 5.0f);
      push(MeshEvent::DELIVERED, p.key, name, m.text);
      if (_bot) scheduleReply(p.key, m.text);
      _pending.erase(_pending.begin() + i);
      continue;
    }
    if ((int32_t)(now - p.deadline) >= 0) {
      if (m.attempt < 3) {
        if (m.attempt == 2) {   // last chance: forget the path, flood
          if (Contact* c = findMut(p.key)) c->v.hops = -1;
        }
        transmit(p, m);
        _store.update(p.key, p.seq, m);
      } else {
        m.state = MSG_FAILED;
        _store.update(p.key, p.seq, m);
        push(MeshEvent::FAILED, p.key, name, m.text);
        _pending.erase(_pending.begin() + i);
        continue;
      }
    }
    i++;
  }
  _store.flush();
}

bool DemoMesh::contactAt(int i, ContactView& out) {
  if (i < 0 || i >= (int)_contacts.size()) return false;
  out = _contacts[i].v;
  return true;
}

bool DemoMesh::findContact(const ConvKey& key, ContactView& out) {
  Contact* c = findMut(key);
  if (!c) return false;
  out = c->v;
  return true;
}

bool DemoMesh::setFavourite(const ConvKey& key, bool fav) {
  Contact* c = findMut(key);
  if (!c) return false;
  c->v.favourite = fav;
  sortContacts();
  return true;
}

bool DemoMesh::removeContact(const ConvKey& key) {
  auto it = std::find_if(_contacts.begin(), _contacts.end(), [&](const Contact& c) { return c.v.key == key; });
  if (it == _contacts.end()) return false;
  _contacts.erase(it);
  return true;
}

bool DemoMesh::resetPath(const ConvKey& key) {
  Contact* c = findMut(key);
  if (!c) return false;
  c->v.hops = -1;
  return true;
}

bool DemoMesh::sendAdvert(bool flood, bool withPosition) {
  sentPackets++;
  recordTx(wire::PL_ADVERT, keyFromName(ConvKey::DIRECT, selfName()), flood);
  bool pos = withPosition && gps && gps->state().valid;
  if (pos) advertsWithPosition++;
  if (pos) logf("advert (%s) with position %.5f, %.5f", flood ? "flood" : "zero hop", gps->state().lat, gps->state().lon);
  else logf("advert (%s)", flood ? "flood" : "zero hop");
  return true;
}

bool DemoMesh::joinChannel(const char* name) {
  for (auto& ch : _channels) if (ch.name == name) return true;
  if (_channels.size() >= 8) return false;
  Channel ch{keyFromName(ConvKey::CHANNEL, name), name};
  _channels.push_back(ch);
  _store.ensure(ch.key, name);
  return true;
}

bool DemoMesh::leaveChannel(const ConvKey& key) {
  auto it = std::find_if(_channels.begin(), _channels.end(), [&](const Channel& c) { return c.key == key; });
  if (it == _channels.end()) return false;
  _channels.erase(it);
  _store.remove(key);
  return true;
}

bool DemoMesh::pollEvent(MeshEvent& out) {
  if (_events.empty()) return false;
  out = _events.front();
  _events.pop_front();
  return true;
}

int DemoMesh::neighbours(NeighbourInfo* out, int max) {
  std::vector<const Contact*> v;
  for (auto& c : _contacts) v.push_back(&c);
  std::sort(v.begin(), v.end(), [](const Contact* a, const Contact* b) { return a->heardAtMs > b->heardAtMs; });
  int n = 0;
  for (auto* c : v) {
    if (n >= max) break;
    utf8Copy(out[n].name, sizeof(out[n].name), c->v.name);
    out[n].snr = c->snr;
    out[n].lastHeardSecs = (_sys.millis() - c->heardAtMs) / 1000;
    n++;
  }
  return n;
}

RadioStats DemoMesh::radioStats() {
  const UiSettings& s = _settings.get();
  const RadioPreset& p = RADIO_PRESETS[s.radioPreset < RADIO_PRESET_COUNT ? s.radioPreset : 0];
  RadioStats r;
  r.rxPackets = _rx;
  r.txPackets = sentPackets;
  r.freqMHz = p.freqMHz;
  r.bandwidthKHz = p.bwKHz;
  r.spreadingFactor = p.sf;
  r.txPowerDbm = s.txPowerDbm;
  r.lastSnr = 6.5f;
  r.lastRssi = -92;
  r.txAirMsHour = _airtime.lastHourMs();
  r.txAirPercent = _airtime.percent();
  r.txAirLimitPercent = AirtimeMeter::limitPercent(p.freqMHz);
  return r;
}

// ================================================================ demo bot

namespace {
// Demo chatter in both UI languages {English, French}
struct Line { const char* en; const char* fr; };
const Line CHATTER[] = {
  {"Can anyone hear the Pibrac repeater?", "Quelqu'un capte le relais de Pibrac ?"},
  {"Range test from Tournefeuille, 3 hops.", "Test de portée depuis Tournefeuille, 3 sauts."},
  {"New antenna on the roof, it reaches far!", "Nouvelle antenne sur le toit, ça porte loin !"},
  {"SNR at 8 dB this morning, great.", "SNR à 8 dB ce matin, top."},
  {"I'll be in Colomiers this afternoon.", "Je passe à Colomiers cet après-midi."},
  {"Who's coming to the MeshCore meetup on Saturday?", "Qui vient à la rencontre MeshCore samedi ?"},
  {"The park repeater is back online.", "Le relais du parc est revenu en ligne."},
  {"Have a nice evening, everyone!", "Bonne soirée à tous !"},
};
const Line REPLIES[] = {
  {"Loud and clear!", "Reçu 5 sur 5 !"},
  {"Got it, the Colomiers repeater works great.", "Bien reçu, le relais de Colomiers marche super."},
  {"Great, I'm receiving you over 2 hops.", "Top, je te reçois à 2 sauts."},
  {"Out for a walk, I'll reply later.", "Je suis en balade, je te réponds plus tard."},
  {"Sounds good!", "Ça marche !"},
};
const Line ANSWERS[] = {
  {"Good question, let me check.", "Bonne question, je regarde ça."},
  {"I think so!", "Je pense que oui !"},
  {"No idea, ask on Public.", "Aucune idée, demande sur Public."},
};
const Line DIRECT[] = {
  {"Did you get my position?", "Tu as bien reçu ma position ?"},
  {"Meet at the park around 6pm?", "On se retrouve au parc vers 18h ?"},
  {"Your Cardputer seems to work perfectly!", "Ton Cardputer marche nickel on dirait !"},
};
template <size_t N> const char* pick(const Line (&a)[N], uint32_t r) { return tr(a[r % N].en, a[r % N].fr); }

bool containsAny(std::string t, std::initializer_list<const char*> words) {
  for (auto& c : t) c = (char)tolower((unsigned char)c);
  for (const char* w : words) if (t.find(w) != std::string::npos) return true;
  return false;
}
}  // namespace

uint32_t DemoMesh::rnd() {
  _rng = _rng * 1664525u + 1013904223u;   // LCG, good enough for a demo
  return _rng >> 8;
}

void DemoMesh::enableBot(uint32_t seed) {
  _bot = true;
  _rng = seed | 1;
  _nextChatter = _sys.millis() + 15000;
}

void DemoMesh::seedDemo() {
  if (_contacts.empty()) {
    addContact("Bob", 1, true, CONTACT_CHAT, true, 43.6047, 1.4442);          // Toulouse, Capitole
    addContact("Alice", 2, false, CONTACT_CHAT, true, 43.6170, 1.2850);       // Pibrac
    addContact("Chloé", 3, false, CONTACT_CHAT, true, 43.6378, 1.3906);       // Blagnac
    addContact("Relais-Colomiers", 0, false, CONTACT_REPEATER, true, 43.6108, 1.3409);
    addContact("Relais-Pibrac", 1, false, CONTACT_REPEATER, false, 0, 0);
  }
  ConvKey bob = keyByName("Bob");
  if (!_store.find(bob)) {
    receiveChannel("Public", "Chloé", tr("Hi everyone, great range today!", "Salut tout le monde, belle portée aujourd'hui !"));
    receive("Bob", tr("Welcome to the demo! Write to me, I'll answer. (No radio: everything is simulated.)",
                      "Bienvenue dans la démo ! Écris-moi, je réponds. (Pas de radio : tout est simulé.)"));
  }
}

void DemoMesh::scheduleReply(const ConvKey& to, const std::string& text) {
  Reply r;
  r.key = to;
  r.at = _sys.millis() + 3000 + rnd() % 4000;
  if (containsAny(text, {"salut", "bonjour", "coucou", "hello", "hi ", "hey"}))
    r.text = tr("Hi! Is the reception good where you are?", "Salut ! Ça capte bien chez toi ?");
  else if (containsAny(text, {"merci", "ok", "parfait", "thanks", "thank you", "great"}))
    r.text = tr("You're welcome!", "Avec plaisir !");
  else if (text.find('?') != std::string::npos) r.text = pick(ANSWERS, rnd());
  else r.text = pick(REPLIES, rnd());
  _replies.push_back(r);
}

void DemoMesh::botTick() {
  uint32_t now = _sys.millis();
  for (size_t i = 0; i < _replies.size();) {
    if ((int32_t)(now - _replies[i].at) < 0) { i++; continue; }
    Reply r = _replies[i];
    _replies.erase(_replies.begin() + i);
    if (r.key.kind == ConvKey::CHANNEL) {
      for (auto& ch : _channels) {
        if (ch.key == r.key) receiveChannel(ch.name, rnd() % 2 ? "Alice" : "Chloé", r.text);
      }
    } else if (Contact* c = findMut(r.key)) {
      if (c->v.type == CONTACT_CHAT) receive(c->v.name, r.text);
    }
  }
  if ((int32_t)(now - _nextChatter) < 0) return;
  _nextChatter = now + 40000 + rnd() % 50000;   // something happens every 40 to 90 s
  uint32_t r = rnd() % 10;
  if (r < 6) {
    static const char* const who[] = {"Alice", "Chloé", "Bob"};
    receiveChannel("Public", who[rnd() % 3], pick(CHATTER, rnd()));
  } else if (r < 8) {
    receive(rnd() % 2 ? "Alice" : "Chloé", pick(DIRECT, rnd()));
  } else {
    hear(rnd() % 2 ? "Relais-Colomiers" : "Relais-Pibrac", 4.0f + (rnd() % 30) / 4.0f);
  }
}
