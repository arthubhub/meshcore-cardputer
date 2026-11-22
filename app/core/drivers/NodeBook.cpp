#include "NodeBook.h"
#include "../Log.h"
#include <string.h>

static const char* TAG = "NODES";

NodeBook::NodeBook(NodeArchive& archive, Lock* queueLock, Lock* archiveLock)
    : _a(archive), _ql(queueLock ? *queueLock : _noLock), _al(archiveLock ? *archiveLock : _noLock) {}

void NodeBook::setPolicy(const Policy& p) {
  LockGuard g(_ql);
  _p = p;
  if (_p.highWater < 1 || _p.highWater > QUEUE) _p.highWater = QUEUE;
}

int NodeBook::find(const uint8_t* key) const {
  for (int i = 0; i < QUEUE; i++) {
    if (_q[i].used && memcmp(_q[i].r.pubKey, key, 32) == 0) return i;
  }
  return -1;
}

int NodeBook::slot(const uint8_t* key, uint32_t nowMs) {
  const int i = find(key);
  if (i >= 0) {
    if (_q[i].writing) _q[i].touched = true;
    _stats.merged++;
    return i;
  }
  for (int k = 0; k < QUEUE; k++) {
    if (_q[k].used) continue;
    Entry& e = _q[k];
    e = Entry();
    memcpy(e.r.pubKey, key, 32);
    e.sinceMs = nowMs;
    e.used = true;
    return k;
  }
  _stats.dropped++;
  return -1;
}

bool NodeBook::heard(const NodeRecord& r, uint32_t nowMs) {
  LockGuard g(_ql);
  _stats.heard++;
  const int i = slot(r.pubKey, nowMs);
  if (i < 0) return false;
  Entry& e = _q[i];
  if (e.remove) {                      // heard again after being forgotten: a new hearing
    e.remove = false;
    e.set = e.clear = 0;
  }
  if (!e.hasHearing) {
    e.r = r;
    if (!e.r.firstHeard) e.r.firstHeard = r.lastHeard;   // the first hearing, before later ones merge in
    e.hasHearing = true;
  } else {
    NodeArchive::merge(e.r, r);        // the change keeps its age: it is written on time
  }
  return true;
}

bool NodeBook::markFlags(const uint8_t* key, uint8_t set, uint8_t clear, uint32_t nowMs) {
  LockGuard g(_ql);
  const int i = slot(key, nowMs);
  if (i < 0) return false;
  Entry& e = _q[i];
  e.set = (uint8_t)((e.set | set) & ~clear);
  e.clear = (uint8_t)((e.clear | clear) & ~set);
  return true;
}

bool NodeBook::forget(const uint8_t* key, uint32_t nowMs) {
  LockGuard g(_ql);
  const int i = slot(key, nowMs);
  if (i < 0) return false;
  Entry& e = _q[i];
  e.remove = true;
  e.hasHearing = false;
  e.set = e.clear = 0;
  return true;
}

int NodeBook::oldestIdle() const {
  int o = -1;
  for (int i = 0; i < QUEUE; i++) {
    if (_q[i].used && !_q[i].writing && (o < 0 || (int32_t)(_q[i].sinceMs - _q[o].sinceMs) < 0)) o = i;
  }
  return o;
}

int NodeBook::busiestIdle() const {
  int best = -1, bestCount = 0;
  for (int i = 0; i < QUEUE; i++) {
    if (!_q[i].used || _q[i].writing) continue;
    const uint16_t b = NodeArchive::bucketOf(_q[i].r.pubKey);
    int n = 0;
    for (int k = 0; k < QUEUE; k++) n += _q[k].used && !_q[k].writing && NodeArchive::bucketOf(_q[k].r.pubKey) == b;
    if (n > bestCount) {
      best = i;
      bestCount = n;
    }
  }
  return best;
}

bool NodeBook::crowded() {
  LockGuard g(_ql);
  return countIdle() >= _p.highWater;
}

int NodeBook::countIdle() const {
  int n = 0;
  for (const Entry& e : _q) n += e.used && !e.writing;
  return n;
}

int NodeBook::pending() {
  LockGuard g(_ql);
  int n = 0;
  for (const Entry& e : _q) n += e.used;
  return n;
}

NodeBook::Stats NodeBook::stats() {
  LockGuard g(_ql);
  return _stats;
}

bool NodeBook::writeBucket(uint16_t bucket) {
  NodeArchive::Op ops[BATCH];
  int idx[BATCH];
  int n = 0;
  {
    LockGuard g(_ql);                  // copy the changes out: the queue stays free during the write
    for (int i = 0; i < QUEUE && n < BATCH; i++) {
      Entry& e = _q[i];
      if (!e.used || e.writing || NodeArchive::bucketOf(e.r.pubKey) != bucket) continue;
      e.writing = true;
      e.touched = false;
      _batch[n] = e.r;
      NodeArchive::Op& op = ops[n];
      op.record = &_batch[n];
      op.kind = e.remove ? NodeArchive::Op::Remove : e.hasHearing ? NodeArchive::Op::Heard : NodeArchive::Op::Flags;
      op.set = e.set;
      op.clear = e.clear;
      idx[n++] = i;
    }
  }
  if (!n) return true;
  NodeArchive::Result results[BATCH];
  bool ok;
  {
    LockGuard g(_al);
    ok = _a.applyBatch(ops, n, results);
  }
  LockGuard g(_ql);
  _stats.writes++;
  for (int k = 0; k < n; k++) {
    Entry& e = _q[idx[k]];
    switch (results[k]) {
      case NodeArchive::Result::Added:
      case NodeArchive::Result::Replaced: _stats.added++; break;
      case NodeArchive::Result::Updated: _stats.updated++; break;
      case NodeArchive::Result::Unchanged: _stats.unchanged++; break;
      case NodeArchive::Result::Refused: _stats.refused++; break;
      case NodeArchive::Result::Failed: _stats.failed++; break;
    }
    e.writing = false;
    if (results[k] == NodeArchive::Result::Failed) continue;   // waits for the next try
    if (e.touched) {
      // Changed during the write: what was written is on flash; keep only what came after. A hearing merged into the
      // entry is still complete (merge is idempotent), so writing it again is harmless; flags and removal also.
      e.touched = false;
      continue;
    }
    e.used = false;
  }
  if (!ok) CORE_LOGW(TAG, "bucket %02x not written: %d change(s) wait for the next try", (unsigned)bucket, n);
  return ok;
}

bool NodeBook::flushOne(uint32_t nowMs) {
  uint16_t bucket;
  {
    LockGuard g(_ql);
    const int o = oldestIdle();
    if (o < 0) return false;
    const bool crowded = countIdle() >= _p.highWater;
    if (!crowded && _wrote && nowMs - _lastWriteMs < _p.minGapMs) return false;
    if (!crowded && nowMs - _q[o].sinceMs < _p.maxAgeMs) return false;
    bucket = NodeArchive::bucketOf(_q[crowded ? busiestIdle() : o].r.pubKey);
    _lastWriteMs = nowMs;
    _wrote = true;
  }
  writeBucket(bucket);
  return true;
}

int NodeBook::flushAll() {
  int writes = 0;
  for (int guard = 0; guard < QUEUE; guard++) {
    uint16_t bucket;
    {
      LockGuard g(_ql);
      const int o = oldestIdle();
      if (o < 0) break;
      bucket = NodeArchive::bucketOf(_q[o].r.pubKey);
    }
    const bool ok = writeBucket(bucket);
    writes++;
    if (!ok) break;                    // the flash refuses: keep the rest in RAM
  }
  return writes;
}

bool NodeBook::get(const uint8_t* key, NodeRecord& out) {
  Entry w;
  bool waiting = false;
  {
    LockGuard g(_ql);
    const int i = find(key);
    if (i >= 0) {
      w = _q[i];
      waiting = true;
    }
  }
  bool stored;
  {
    LockGuard g(_al);
    stored = _a.get(key, out);
  }
  if (!waiting) return stored;
  if (w.remove) return false;
  if (w.hasHearing) {
    if (stored) NodeArchive::merge(out, w.r);
    else out = NodeArchive::fresh(w.r);
    stored = true;
  }
  if (stored) out.flags = (uint8_t)((out.flags | w.set) & ~w.clear);
  return stored;
}

int NodeBook::findByHash(const uint8_t* hash, int bytes, uint8_t typeMask, NodeRecord* out, int max) {
  if (!hash || bytes < 1 || bytes > 32) return 0;
  // The waiting changes of that ID, copied under the queue's lock (a bucket holds few of them)
  Entry waiting[BATCH];
  int nw = 0;
  {
    LockGuard g(_ql);
    for (int i = 0; i < QUEUE && nw < BATCH; i++) {
      if (_q[i].used && memcmp(_q[i].r.pubKey, hash, (size_t)bytes) == 0) waiting[nw++] = _q[i];
    }
  }
  LockGuard g(_al);
  int found = _a.findByHash(hash, bytes, typeMask, out, max);
  for (int k = 0; k < nw; k++) {
    const Entry& w = waiting[k];
    NodeRecord stored;
    const bool onFlash = _a.get(w.r.pubKey, stored);   // the bucket is in the archive's buffer: no flash read
    int pos = -1;
    for (int j = 0; out && j < found && j < max; j++) {
      if (memcmp(out[j].pubKey, w.r.pubKey, 32) == 0) pos = j;
    }
    if (w.remove) {
      if (onFlash && (NodeArchive::typeBit(stored.type) & typeMask)) {
        const int shown = found < max ? found : max;   // listed by the archive, but forgotten: take it out
        if (pos >= 0) {
          for (int j = pos; j + 1 < shown; j++) out[j] = out[j + 1];
        }
        found--;
      }
      continue;
    }
    NodeRecord r;
    if (onFlash) {
      r = stored;
      if (w.hasHearing) NodeArchive::merge(r, w.r);
    } else if (w.hasHearing) {
      r = NodeArchive::fresh(w.r);
    } else {
      continue;                        // flags for a node not stored: nothing to show
    }
    r.flags = (uint8_t)((r.flags | w.set) & ~w.clear);
    const bool match = (NodeArchive::typeBit(r.type) & typeMask) != 0;
    if (pos >= 0) {
      out[pos] = r;                    // refresh the copy handed out
    } else if (!onFlash && match) {
      if (out && found < max) out[found] = r;
      found++;
    }
  }
  return found;
}
