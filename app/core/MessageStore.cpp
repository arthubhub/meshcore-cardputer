#include "MessageStore.h"
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t RING_MAGIC = 0x314D5043;    // "CPM1"
constexpr uint32_t INDEX_MAGIC = 0x31495043;   // "CPI1"
constexpr const char* INDEX_PATH = "/mindex";

struct RingHeader {
  uint32_t magic;
  uint32_t total;
  uint32_t recordSize;
  uint32_t slots;
};

struct IndexHeader {
  uint32_t magic;
  uint32_t recordSize;
  uint32_t count;
};

uint32_t slotOffset(uint32_t seq) {
  return sizeof(RingHeader) + (seq % MessageStore::MAX_MSGS) * sizeof(StoredMsg);
}
}  // namespace

void utf8Copy(char* dst, size_t cap, const char* src) {
  if (!cap) return;
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && (src[n] & 0xC0) == 0x80) n--;   // don't cut a multi-byte sequence
  }
  memcpy(dst, src, n);
  dst[n] = 0;
}

bool ConvKey::operator==(const ConvKey& o) const {
  return kind == o.kind && memcmp(id, o.id, sizeof(id)) == 0;
}

void ConvKey::path(char* out, size_t n) const {
  snprintf(out, n, "/m%c%02x%02x%02x%02x%02x%02x", kind == CHANNEL ? 'c' : 'd', id[0], id[1], id[2], id[3], id[4],
           id[5]);
}

void MessageStore::begin(Storage* st) {
  _st = st;
  _count = 0;
  _dirty = false;
  _sorted = false;
  for (auto& c : _convs) c = ConvMeta();
  IndexHeader h;
  if (!_st->read(INDEX_PATH, 0, &h, sizeof(h)) || h.magic != INDEX_MAGIC || h.recordSize != sizeof(ConvMeta) ||
      h.count > MAX_CONVS) {
    return;   // no index yet (or from an incompatible version): start empty
  }
  if (!_st->read(INDEX_PATH, sizeof(h), _convs, h.count * sizeof(ConvMeta))) return;
  _count = h.count;
  for (int i = 0; i < _count; i++) {
    _convs[i].name[sizeof(_convs[i].name) - 1] = 0;
    _convs[i].last[sizeof(_convs[i].last) - 1] = 0;
  }
}

bool MessageStore::flush() {
  if (!_dirty || !_st) return true;
  IndexHeader h{INDEX_MAGIC, sizeof(ConvMeta), (uint32_t)_count};
  bool ok = _st->write(INDEX_PATH, 0, &h, sizeof(h));
  ok = ok && (_count == 0 || _st->write(INDEX_PATH, sizeof(h), _convs, _count * sizeof(ConvMeta)));
  if (ok) _dirty = false;
  return ok;
}

ConvMeta* MessageStore::findMut(const ConvKey& k) {
  for (int i = 0; i < _count; i++) if (_convs[i].key == k) return &_convs[i];
  return nullptr;
}

const ConvMeta* MessageStore::find(const ConvKey& k) const {
  for (int i = 0; i < _count; i++) if (_convs[i].key == k) return &_convs[i];
  return nullptr;
}

ConvMeta* MessageStore::ensure(const ConvKey& k, const char* name) {
  if (!k.valid()) return nullptr;
  if (ConvMeta* c = findMut(k)) {
    if (name && *name && strcmp(c->name, name) != 0) { utf8Copy(c->name, sizeof(c->name), name); _dirty = true; }
    return c;
  }
  if (_count >= _capacity) {
    // Full: forget the direct conversation with the oldest activity (channels are kept).
    int victim = -1;
    for (int i = 0; i < _count; i++) {
      if (_convs[i].key.kind == ConvKey::DIRECT && (victim < 0 || _convs[i].lastTs < _convs[victim].lastTs)) victim = i;
    }
    if (victim < 0) return nullptr;
    remove(_convs[victim].key);
  }
  ConvMeta& c = _convs[_count++];
  c = ConvMeta();
  c.key = k;
  c.used = 1;
  utf8Copy(c.name, sizeof(c.name), name ? name : "?");
  // Keep the sequence numbering if a ring file survived (e.g. index lost)
  char path[24];
  k.path(path, sizeof(path));
  RingHeader rh;
  if (_st->read(path, 0, &rh, sizeof(rh)) && rh.magic == RING_MAGIC && rh.recordSize == sizeof(StoredMsg) &&
      rh.slots == MAX_MSGS) {
    c.total = rh.total;
  }
  _dirty = true;
  _sorted = false;
  return &c;
}

bool MessageStore::writeHeader(const char* path, uint32_t total) {
  RingHeader rh{RING_MAGIC, total, sizeof(StoredMsg), MAX_MSGS};
  return _st->write(path, 0, &rh, sizeof(rh));
}

int32_t MessageStore::append(const ConvKey& k, const char* name, const StoredMsg& m, bool unread) {
  ConvMeta* c = ensure(k, name);
  if (!c) return -1;
  char path[24];
  k.path(path, sizeof(path));
  uint32_t seq = c->total;
  if (!_st->write(path, slotOffset(seq), &m, sizeof(m))) return -1;
  if (!writeHeader(path, seq + 1)) return -1;
  c->total = seq + 1;
  c->lastTs = m.ts;
  c->lastOutgoing = m.outgoing;
  if (m.sender[0]) {
    char tmp[sizeof(m.sender) + 2 + sizeof(m.text)];
    snprintf(tmp, sizeof(tmp), "%s: %s", m.sender, m.text);
    utf8Copy(c->last, sizeof(c->last), tmp);
  } else {
    utf8Copy(c->last, sizeof(c->last), m.text);
  }
  if (unread && c->unread < 0xFFFF) c->unread++;
  _dirty = true;
  _sorted = false;
  return (int32_t)seq;
}

bool MessageStore::read(const ConvKey& k, uint32_t seq, StoredMsg& out) {
  const ConvMeta* c = find(k);
  if (!c || seq >= c->total || seq + MAX_MSGS < c->total) return false;   // unknown or overwritten
  char path[24];
  k.path(path, sizeof(path));
  return _st->read(path, slotOffset(seq), &out, sizeof(out));
}

bool MessageStore::update(const ConvKey& k, uint32_t seq, const StoredMsg& m) {
  const ConvMeta* c = find(k);
  if (!c || seq >= c->total || seq + MAX_MSGS < c->total) return false;
  char path[24];
  k.path(path, sizeof(path));
  return _st->write(path, slotOffset(seq), &m, sizeof(m));
}

uint32_t MessageStore::total(const ConvKey& k) {
  const ConvMeta* c = find(k);
  if (!c) return 0;
  return c->total < (uint32_t)MAX_MSGS ? c->total : MAX_MSGS;
}

int MessageStore::loadRecent(const ConvKey& k, StoredMsg* out, int max, int skipNewest) {
  const ConvMeta* c = find(k);
  if (!c || max <= 0) return 0;
  uint32_t avail = total(k);
  if (skipNewest < 0) skipNewest = 0;
  if ((uint32_t)skipNewest >= avail) return 0;
  uint32_t end = c->total - skipNewest;   // exclusive
  uint32_t n = avail - skipNewest;
  if (n > (uint32_t)max) n = max;
  uint32_t first = end - n;
  char path[24];
  k.path(path, sizeof(path));
  // At most two contiguous reads (the ring may wrap)
  uint32_t done = 0;
  while (done < n) {
    uint32_t seq = first + done;
    uint32_t slot = seq % MAX_MSGS;
    uint32_t chunk = n - done;
    if (slot + chunk > (uint32_t)MAX_MSGS) chunk = MAX_MSGS - slot;
    if (!_st->read(path, slotOffset(seq), &out[done], chunk * sizeof(StoredMsg))) return done;
    done += chunk;
  }
  for (uint32_t i = 0; i < n; i++) {
    out[i].text[MSG_TEXT_MAX] = 0;
    out[i].sender[sizeof(out[i].sender) - 1] = 0;
  }
  return n;
}

void MessageStore::markRead(const ConvKey& k) {
  ConvMeta* c = findMut(k);
  if (c && c->unread) { c->unread = 0; _dirty = true; }
}

void MessageStore::rename(const ConvKey& k, const char* name) {
  ConvMeta* c = findMut(k);
  if (c && name && strcmp(c->name, name)) { utf8Copy(c->name, sizeof(c->name), name); _dirty = true; }
}

void MessageStore::remove(const ConvKey& k) {
  for (int i = 0; i < _count; i++) {
    if (_convs[i].key == k) {
      char path[24];
      k.path(path, sizeof(path));
      _st->remove(path);
      for (int j = i; j < _count - 1; j++) _convs[j] = _convs[j + 1];
      _count--;
      _convs[_count] = ConvMeta();
      _dirty = true;
      _sorted = false;
      return;
    }
  }
}

int MessageStore::unreadTotal() const {
  int n = 0;
  for (int i = 0; i < _count; i++) n += _convs[i].unread;
  return n;
}

void MessageStore::sort() {
  for (int i = 0; i < _count; i++) _order[i] = i;
  // insertion sort: tiny array, mostly sorted already
  for (int i = 1; i < _count; i++) {
    uint8_t v = _order[i];
    int j = i - 1;
    while (j >= 0 && _convs[_order[j]].lastTs < _convs[v].lastTs) { _order[j + 1] = _order[j]; j--; }
    _order[j + 1] = v;
  }
  _sorted = true;
}

const ConvMeta* MessageStore::at(int i) {
  if (i < 0 || i >= _count) return nullptr;
  if (!_sorted) sort();
  return &_convs[_order[i]];
}
