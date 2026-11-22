#include "NodeArchive.h"
#include "ContactPosition.h"
#include "../Log.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "NODES";

BucketStore::Layout NodeArchive::layout(const char* prefix, uint8_t dirs) {
  BucketStore::Layout l;
  l.prefix = prefix;
  l.dirs = dirs;
  l.magic = NodeArchive::MAGIC;
  l.version = NodeRecordCodec::VERSION;
  l.recordSize = NodeRecordCodec::BYTES;
  l.buckets = NodeArchive::BUCKETS;
  l.maxRecords = NodeArchive::PER_BUCKET;
  return l;
}

NodeArchive::NodeArchive(Storage& storage, const char* prefix, uint8_t dirs) : _store(storage, layout(prefix, dirs)) {
  memset(_counts, -1, sizeof(_counts));
}

const char* NodeArchive::resultName(Result r) {
  switch (r) {
    case Result::Added: return "added";
    case Result::Replaced: return "replaced";
    case Result::Updated: return "updated";
    case Result::Unchanged: return "unchanged";
    case Result::Refused: return "refused";
    case Result::Failed: return "failed";
  }
  return "?";
}

void NodeArchive::keyText(const uint8_t* key, char* out) {
  snprintf(out, 7, "%02x%02x%02x", key[0], key[1], key[2]);
}

bool NodeArchive::load(uint16_t b) {
  if (_bucket == (int)b) return true;
  _bucket = -1;
  uint16_t count = 0;
  const BucketStore::Status s = _store.load(b, _buf, count);
  if (s == BucketStore::Status::Failed) return false;
  _bucket = b;
  _count = count;                       // a set-aside bucket reads as empty (BucketStore logged why)
  _counts[b] = (int8_t)count;
  return true;
}

bool NodeArchive::save() {
  if (_bucket < 0) return false;
  if (!_store.save((uint16_t)_bucket, _buf, _count)) {
    _bucket = -1;                       // the buffer no longer matches the flash: read it again next time
    return false;
  }
  _counts[_bucket] = (int8_t)_count;
  return true;
}

NodeRecord NodeArchive::at(int i) const {
  NodeRecord r;
  NodeRecordCodec::decode(_buf + (size_t)i * NodeRecordCodec::BYTES, r);
  return r;
}

void NodeArchive::put(int i, const NodeRecord& r) {
  NodeRecordCodec::encode(r, _buf + (size_t)i * NodeRecordCodec::BYTES);
}

int NodeArchive::indexOf(const uint8_t* key) const {
  for (int i = 0; i < _count; i++) {
    if (memcmp(_buf + (size_t)i * NodeRecordCodec::BYTES, key, 32) == 0) return i;
  }
  return -1;
}

bool NodeArchive::weaker(const NodeRecord& a, const NodeRecord& b) {
  const bool ia = a.has(NodeRecord::Imported), ib = b.has(NodeRecord::Imported);
  if (ia != ib) return ia;              // never heard goes first
  if (a.lastHeard != b.lastHeard) return a.lastHeard < b.lastHeard;
  return a.hops > b.hops;               // farther (unknown counts as farthest)
}

int NodeArchive::weakest() const {
  int w = -1;
  NodeRecord wr;
  for (int i = 0; i < _count; i++) {
    NodeRecord r = at(i);
    if (r.isProtected()) continue;
    if (w < 0 || weaker(r, wr)) {
      w = i;
      wr = r;
    }
  }
  return w;
}

NodeRecord NodeArchive::fresh(const NodeRecord& heard) {
  NodeRecord r = heard;
  r.flags = heard.flags & NodeRecord::Imported;   // the other flags come from setFlags()
  r.name[sizeof(r.name) - 1] = 0;
  if (!r.firstHeard) r.firstHeard = r.lastHeard;
  ContactPosition pos;
  if (heard.hasPos() && pos.offer(heard.latE6, heard.lonE6, heard.posTime, (PositionSource)heard.posSource)) {
    r.flags |= NodeRecord::HasPos;
  } else {
    r.latE6 = r.lonE6 = 0;
    r.posTime = 0;
    r.posSource = 0;
  }
  return r;
}

bool NodeArchive::merge(NodeRecord& s, const NodeRecord& h) {
  bool due = false;                     // something that matters changed
  const bool imported = h.has(NodeRecord::Imported);

  if (!imported) {
    if (h.name[0] && strncmp(s.name, h.name, sizeof(s.name) - 1) != 0) {
      memcpy(s.name, h.name, sizeof(s.name));
      s.name[sizeof(s.name) - 1] = 0;
      due = true;
    }
    if (h.type && h.type != s.type) {
      s.type = h.type;
      due = true;
    }
    if (s.has(NodeRecord::Imported)) {  // heard at last
      s.flags &= ~NodeRecord::Imported;
      due = true;
    }
  } else if (!s.name[0] && h.name[0]) {  // an import only fills what is missing
    memcpy(s.name, h.name, sizeof(s.name));
    s.name[sizeof(s.name) - 1] = 0;
    due = true;
  }

  if (h.hasPos()) {
    ContactPosition pos;
    if (s.hasPos()) {
      pos.latE6 = s.latE6;
      pos.lonE6 = s.lonE6;
      pos.time = s.posTime;
      pos.source = s.posSource ? (PositionSource)s.posSource : PositionSource::Advert;
    }
    if (pos.offer(h.latE6, h.lonE6, h.posTime, (PositionSource)h.posSource) &&
        (pos.latE6 != s.latE6 || pos.lonE6 != s.lonE6 || pos.time != s.posTime || !s.hasPos())) {
      s.latE6 = pos.latE6;
      s.lonE6 = pos.lonE6;
      s.posTime = pos.time;
      s.posSource = (uint8_t)pos.source;
      s.flags |= NodeRecord::HasPos;
      due = true;
    }
  }

  if (h.lastHeard >= s.lastHeard && h.lastHeard) {   // the signal of the newest hearing
    s.hops = h.hops;
    s.snrX4 = h.snrX4;
    s.rssi = h.rssi;
  }
  if (h.firstHeard && (!s.firstHeard || h.firstHeard < s.firstHeard)) s.firstHeard = h.firstHeard;
  if (h.lastHeard && !s.firstHeard) s.firstHeard = h.lastHeard;
  if (h.lastHeard > s.lastHeard) {
    if (h.lastHeard - s.lastHeard >= REFRESH_S) due = true;
    s.lastHeard = h.lastHeard;
  }
  if (h.lastAdvertTs > s.lastAdvertTs) s.lastAdvertTs = h.lastAdvertTs;
  return due;
}

NodeArchive::Result NodeArchive::apply(const NodeRecord& heard, NodeRecord* dropped, bool& dirty) {
  const uint16_t b = bucketOf(heard.pubKey);
  char key[7];
  keyText(heard.pubKey, key);

  const int i = indexOf(heard.pubKey);
  if (i >= 0) {
    NodeRecord s = at(i);
    if (!merge(s, heard)) return Result::Unchanged;   // the buffer keeps matching the flash
    put(i, s);
    dirty = true;
    return Result::Updated;
  }

  const NodeRecord r = fresh(heard);
  if (_count < PER_BUCKET) {
    put(_count++, r);
    dirty = true;
    return Result::Added;
  }

  const int w = weakest();
  if (w < 0) {
    CORE_LOGW(TAG, "bucket %02x full of protected nodes: '%s' (%s) not kept", (unsigned)b, r.name, key);
    return Result::Refused;
  }
  const NodeRecord leaving = at(w);
  if (!weaker(leaving, r)) {
    CORE_LOGI(TAG, "bucket %02x full: '%s' (%s) is not newer than any node in it, not kept", (unsigned)b, r.name, key);
    return Result::Refused;
  }
  put(w, r);
  dirty = true;
  char lk[7];
  keyText(leaving.pubKey, lk);
  CORE_LOGI(TAG, "bucket %02x full: '%s' (%s) leaves for '%s' (%s)", (unsigned)b, leaving.name, lk, r.name, key);
  if (dropped) *dropped = leaving;
  return Result::Replaced;
}

NodeArchive::Result NodeArchive::upsert(const NodeRecord& heard, NodeRecord* dropped) {
  if (!load(bucketOf(heard.pubKey))) return Result::Failed;
  bool dirty = false;
  const Result r = apply(heard, dropped, dirty);
  if (dirty && !save()) return Result::Failed;
  return r;
}

NodeArchive::Result NodeArchive::applyFlags(const uint8_t* key, uint8_t set, uint8_t clear, bool& dirty) {
  const int i = indexOf(key);
  if (i < 0) return Result::Refused;
  NodeRecord r = at(i);
  const uint8_t before = r.flags;
  r.flags = (uint8_t)((r.flags | set) & ~clear);
  if (r.flags == before) return Result::Unchanged;
  put(i, r);
  dirty = true;
  return Result::Updated;
}

NodeArchive::Result NodeArchive::applyRemove(const uint8_t* key, bool& dirty) {
  const int i = indexOf(key);
  if (i < 0) return Result::Refused;
  const size_t rb = NodeRecordCodec::BYTES;
  memmove(_buf + i * rb, _buf + (i + 1) * rb, (size_t)(_count - i - 1) * rb);
  _count--;
  dirty = true;
  return Result::Updated;
}

bool NodeArchive::applyBatch(const Op* ops, int n, Result* results, NodeRecord* dropped) {
  if (n <= 0) return true;
  const uint16_t b = bucketOf(ops[0].record->pubKey);
  for (int i = 1; i < n; i++) {
    if (bucketOf(ops[i].record->pubKey) != b) {
      CORE_LOGE(TAG, "batch of %d changes mixes buckets %02x and %02x: refused", n, (unsigned)b,
                (unsigned)bucketOf(ops[i].record->pubKey));
      for (int k = 0; k < n; k++) results[k] = Result::Failed;
      return false;
    }
  }
  if (!load(b)) {
    for (int k = 0; k < n; k++) results[k] = Result::Failed;
    return false;
  }
  bool dirty = false;
  for (int i = 0; i < n; i++) {
    const Op& op = ops[i];
    switch (op.kind) {
      case Op::Heard:
        results[i] = apply(*op.record, dropped ? &dropped[i] : nullptr, dirty);
        if ((op.set || op.clear) && results[i] != Result::Refused) {
          const Result f = applyFlags(op.record->pubKey, op.set, op.clear, dirty);
          if (f == Result::Updated && results[i] == Result::Unchanged) results[i] = Result::Updated;
        }
        break;
      case Op::Flags: results[i] = applyFlags(op.record->pubKey, op.set, op.clear, dirty); break;
      case Op::Remove: results[i] = applyRemove(op.record->pubKey, dirty); break;
    }
  }
  if (dirty && !save()) {
    for (int i = 0; i < n; i++) {
      if (results[i] != Result::Unchanged && results[i] != Result::Refused) results[i] = Result::Failed;
    }
    return false;
  }
  return true;
}

bool NodeArchive::get(const uint8_t* key, NodeRecord& out) {
  if (!load(bucketOf(key))) return false;
  const int i = indexOf(key);
  if (i < 0) return false;
  out = at(i);
  return true;
}

int NodeArchive::findByHash(const uint8_t* hash, int bytes, uint8_t typeMask, NodeRecord* out, int max) {
  if (!hash || bytes < 1 || bytes > 32 || !load(bucketOf(hash))) return 0;
  int found = 0;
  for (int i = 0; i < _count; i++) {
    const uint8_t* rec = _buf + (size_t)i * NodeRecordCodec::BYTES;
    if (memcmp(rec, hash, (size_t)bytes) != 0) continue;
    NodeRecord r = at(i);
    if (!(typeBit(r.type) & typeMask)) continue;
    if (out && found < max) out[found] = r;
    found++;
  }
  return found;
}

bool NodeArchive::setFlags(const uint8_t* key, uint8_t set, uint8_t clear) {
  if (!load(bucketOf(key))) return false;
  bool dirty = false;
  if (applyFlags(key, set, clear, dirty) == Result::Refused) return false;
  return !dirty || save();
}

bool NodeArchive::remove(const uint8_t* key) {
  if (!load(bucketOf(key))) return false;
  bool dirty = false;
  if (applyRemove(key, dirty) == Result::Refused) return false;
  return save();
}

int NodeArchive::scan(int fromBucket, int buckets, NodeVisitor& visitor) {
  if (fromBucket < 0) return -1;
  int b = fromBucket;
  for (; b < BUCKETS && buckets-- > 0; b++) {
    if (!load((uint16_t)b)) continue;   // logged by BucketStore; the other buckets are still visited
    for (int i = 0; i < _count; i++) {
      if (!visitor.visit(at(i))) return -1;
    }
  }
  return b < BUCKETS ? b : -1;
}

int NodeArchive::knownCount() const {
  int n = 0;
  for (int b = 0; b < BUCKETS; b++) {
    if (_counts[b] < 0) return -1;
    n += _counts[b];
  }
  return n;
}

void NodeArchive::forgetBuffer() {
  _bucket = -1;
  memset(_counts, -1, sizeof(_counts));
}
