#include "DemoMesh.h"
#include "RadioPresets.h"
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
  if (!c) { _contacts.push_back(Contact()); c = &_contacts.back(); }
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
  c->heardAtMs = _sys.millis();
  c->snr = 7.25f;
  sortContacts();
}

void DemoMesh::hear(const std::string& name, float snr) {
  ConvKey k = keyFromName(ConvKey::DIRECT, name);
  Contact* c = findMut(k);
  if (!c) {
    addContact(name, 1, false, CONTACT_CHAT, false, 0, 0);
    push(MeshEvent::CONTACT_NEW, k, name, "");
    c = findMut(k);
  }
  c->snr = snr;
  c->heardAtMs = _sys.millis();
  c->v.lastHeard = nowUnix();
  sortContacts();
}

void DemoMesh::receive(const std::string& sender, const std::string& text) {
  hear(sender, 6.5f);
  ConvKey k = keyFromName(ConvKey::DIRECT, sender);
  Contact* c = findMut(k);
  StoredMsg m;
  m.ts = nowUnix();
  m.state = MSG_RECEIVED;
  m.snrX4 = 26;
  m.hops = c ? std::max<int>(0, c->v.hops) : 1;
  utf8Copy(m.text, sizeof(m.text), text.c_str());
  _store.append(k, sender.c_str(), m, true);
  _rx++;
  push(MeshEvent::MESSAGE, k, sender, text);
  logf("rx %s: %s", sender.c_str(), text.c_str());
}

void DemoMesh::receiveChannel(const std::string& channel, const std::string& sender, const std::string& text) {
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
  utf8Copy(m.sender, sizeof(m.sender), sender.c_str());
  utf8Copy(m.text, sizeof(m.text), text.c_str());
  _store.append(k, channel.c_str(), m, true);
  _rx++;
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
  p.deadline = _sys.millis() + 4000;
  p.ackAt = netAcks ? _sys.millis() + netDelayMs : 0;
  sentPackets++;
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

void DemoMesh::tick() {
  uint32_t now = _sys.millis();
  if (_bot) botTick();
  for (size_t i = 0; i < _pending.size();) {
    Pending& p = _pending[i];
    StoredMsg m;
    if (!_store.read(p.key, p.seq, m)) { _pending.erase(_pending.begin() + i); continue; }
    const ConvMeta* meta = _store.find(p.key);
    std::string name = meta ? meta->name : "?";
    if (p.ackAt && (int32_t)(now - p.ackAt) >= 0) {
      m.state = MSG_DELIVERED;
      _store.update(p.key, p.seq, m);
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
  return r;
}

// ================================================================ demo bot

namespace {
const char* const CHATTER[] = {
  "Quelqu'un capte le relais de Pibrac ?",
  "Test de portée depuis Tournefeuille, 3 sauts.",
  "Nouvelle antenne sur le toit, ça porte loin !",
  "SNR à 8 dB ce matin, top.",
  "Je passe à Colomiers cet après-midi.",
  "Qui vient à la rencontre MeshCore samedi ?",
  "Le relais du parc est revenu en ligne.",
  "Bonne soirée à tous !",
};
const char* const REPLIES[] = {
  "Reçu 5 sur 5 !",
  "Bien reçu, le relais de Colomiers marche super.",
  "Top, je te reçois à 2 sauts.",
  "Je suis en balade, je te réponds plus tard.",
  "Ça marche !",
};
const char* const ANSWERS[] = {
  "Bonne question, je regarde ça.",
  "Je pense que oui !",
  "Aucune idée, demande sur Public.",
};
const char* const DIRECT[] = {
  "Tu as bien reçu ma position ?",
  "On se retrouve au parc vers 18h ?",
  "Ton Cardputer marche nickel on dirait !",
};
template <size_t N> const char* pick(const char* const (&a)[N], uint32_t r) { return a[r % N]; }

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
    receiveChannel("Public", "Chloé", "Salut tout le monde, belle portée aujourd'hui !");
    receive("Bob", "Bienvenue dans la démo ! Écris-moi, je réponds. (Pas de radio : tout est simulé.)");
  }
}

void DemoMesh::scheduleReply(const ConvKey& to, const std::string& text) {
  Reply r;
  r.key = to;
  r.at = _sys.millis() + 3000 + rnd() % 4000;
  if (containsAny(text, {"salut", "bonjour", "coucou", "hello"})) r.text = "Salut ! Ça capte bien chez toi ?";
  else if (containsAny(text, {"merci", "ok", "parfait"})) r.text = "Avec plaisir !";
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
