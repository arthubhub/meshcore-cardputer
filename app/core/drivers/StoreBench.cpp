#include "StoreBench.h"
#include "../hal/Storage.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- TimingStats

void TimingStats::add(uint32_t us) {
  if (_n < MAX_SAMPLES) _s[_n] = us;
  if (!_n || us < _min) _min = us;
  if (us > _max) _max = us;
  _sum += us;
  _n++;
}

uint32_t TimingStats::percentile(int pct) const {
  const int n = _n < MAX_SAMPLES ? _n : MAX_SAMPLES;
  if (!n) return 0;
  uint32_t sorted[MAX_SAMPLES];
  memcpy(sorted, _s, sizeof(uint32_t) * (size_t)n);
  for (int i = 1; i < n; i++) {   // insertion sort: 256 values at most, bench only
    uint32_t v = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; j--; }
    sorted[j + 1] = v;
  }
  int idx = (n * pct + 99) / 100 - 1;
  if (idx < 0) idx = 0;
  if (idx >= n) idx = n - 1;
  return sorted[idx];
}

void TimingStats::format(char* out, size_t n) const {
  snprintf(out, n, "n %d, min %.1f, avg %.1f, p95 %.1f, max %.1f ms", _n, min() / 1000.0, avg() / 1000.0,
           percentile(95) / 1000.0, max() / 1000.0);
}

// ---------------------------------------------------------------- StoreBench

StoreBench::StoreBench(Storage& storage, StoreBenchHost& host, const Options& o)
    : _st(storage), _h(host), _o(o), _store(storage, NodeArchive::layout(o.prefix, o.dirs)), _archive(storage, o.prefix, o.dirs),
      _seed(o.seed) {
  if (_o.buckets < 1 || _o.buckets > NodeArchive::BUCKETS) _o.buckets = NodeArchive::BUCKETS;
  if (_o.fullRecords < 1 || _o.fullRecords > NodeArchive::PER_BUCKET) _o.fullRecords = NodeArchive::PER_BUCKET;
  if (_o.smallRecords < 1 || _o.smallRecords > _o.fullRecords) _o.smallRecords = 1;
}

const char* StoreBench::phaseName(Phase p) {
  switch (p) {
    case Phase::Start: return "start";
    case Phase::SmallFill: return "small buckets";
    case Phase::Fill: return "full buckets";
    case Phase::Read: return "read";
    case Phase::Rewrite: return "rewrite";
    case Phase::Lookup: return "lookup";
    case Phase::Clear: return "clear";
    case Phase::Done: return "done";
    case Phase::Failed: return "failed";
  }
  return "?";
}

uint32_t StoreBench::rnd() {
  _seed ^= _seed << 13;
  _seed ^= _seed >> 17;
  _seed ^= _seed << 5;
  return _seed;
}

void StoreBench::fill(uint8_t* buf, int bucket, int records) {
  for (int k = 0; k < records; k++) {
    NodeRecord r;
    for (int j = 0; j < 32; j++) r.pubKey[j] = (uint8_t)rnd();
    r.pubKey[0] = (uint8_t)((bucket << 1) | (rnd() & 1));   // in this bucket
    snprintf(r.name, sizeof(r.name), "bench %02x-%02d %08lx", bucket, k, (unsigned long)rnd());
    r.type = (uint8_t)(1 + rnd() % 4);
    r.hops = (uint8_t)(rnd() % 8);
    r.lastHeard = 1780000000u + rnd() % 100000;
    r.firstHeard = r.lastHeard - rnd() % 100000;
    r.flags = NodeRecord::HasPos;
    r.latE6 = 48000000 + (int32_t)(rnd() % 2000000);
    r.lonE6 = 2000000 + (int32_t)(rnd() % 2000000);
    NodeRecordCodec::encode(r, buf + (size_t)k * NodeRecordCodec::BYTES);
  }
}

void StoreBench::next(Phase p) {
  _phase = p;
  _i = 0;
}

bool StoreBench::fail(const char* what) {
  snprintf(_r.failure, sizeof(_r.failure), "%s, bucket %d", what, _i);
  _r.ok = false;
  _r.failedIn = _phase;
  _r.elapsedUs = _h.micros() - _startUs;
  _phase = Phase::Failed;
  return false;
}

bool StoreBench::step() {
  if (_phase == Phase::Done || _phase == Phase::Failed) return false;
  const uint32_t t0 = _h.micros();
  switch (_phase) {
    case Phase::Start:
      if (!_store.valid()) return fail("bucket layout refused");
      _startUs = t0;
      _r.usedBefore = _h.fsUsedBytes();
      next(Phase::SmallFill);
      break;

    case Phase::SmallFill:
    case Phase::Fill:
    case Phase::Rewrite: {
      const int n = _phase == Phase::SmallFill ? _o.smallRecords : _o.fullRecords;
      fill(_buf, _i, n);
      const uint32_t s = _h.micros();
      const bool ok = _store.save((uint16_t)_i, _buf, (uint16_t)n);
      const uint32_t us = _h.micros() - s;
      if (!ok) return fail("save failed");
      (_phase == Phase::SmallFill ? _r.smallSave : _phase == Phase::Fill ? _r.fullSave : _r.rewrite).add(us);
      if (++_i >= _o.buckets) {
        if (_phase == Phase::SmallFill) {
          _r.usedSmall = _h.fsUsedBytes();
          next(Phase::Fill);
        } else if (_phase == Phase::Fill) {
          _r.usedFull = _h.fsUsedBytes();
          next(Phase::Read);
        } else {
          next(Phase::Lookup);
        }
      }
      break;
    }

    case Phase::Read: {
      if (_i == 0) _readStartUs = t0;
      uint16_t count = 0;
      const uint32_t s = _h.micros();
      const BucketStore::Status st = _store.load((uint16_t)_i, _buf, count);
      const uint32_t us = _h.micros() - s;
      if (st != BucketStore::Status::Ok || count != _o.fullRecords) return fail("read back wrong");
      _r.read.add(us);
      if (++_i >= _o.buckets) {
        _r.fullReadUs = _h.micros() - _readStartUs;
        next(Phase::Rewrite);
      }
      break;
    }

    case Phase::Lookup: {
      uint8_t hash = (uint8_t)rnd();
      hash = (uint8_t)((hash % (uint32_t)(_o.buckets * 2)));   // only the buckets the bench wrote
      const uint32_t s = _h.micros();
      _archive.findByHash(&hash, 1, NodeArchive::ALL_TYPES, nullptr, 0);
      _r.lookup.add(_h.micros() - s);
      if (++_i >= _o.lookups) next(Phase::Clear);
      break;
    }

    case Phase::Clear: {
      const uint32_t s = _h.micros();
      const bool ok = _store.save((uint16_t)_i, nullptr, 0);
      _r.remove.add(_h.micros() - s);
      if (!ok) return fail("remove failed");
      if (++_i >= _o.buckets) {
        _r.usedAfter = _h.fsUsedBytes();
        _r.ok = true;
        _r.elapsedUs = _h.micros() - _startUs;
        next(Phase::Done);
      }
      break;
    }

    case Phase::Done:
    case Phase::Failed:
      break;
  }
  const uint32_t stepUs = _h.micros() - t0;
  if (stepUs > _r.longestStepUs) _r.longestStepUs = stepUs;
  return _phase != Phase::Done && _phase != Phase::Failed;
}

void StoreBench::report() {
  char line[200], st[96];
  snprintf(line, sizeof(line), "store bench '%s' (%u dirs, %s writes): %d buckets, %d then %d records of %d B (file %lu then %lu B)",
           _o.prefix, (unsigned)_o.dirs, _st.atomicWriteFile() ? "atomic" : "temp+rename", _o.buckets, _o.smallRecords, _o.fullRecords, NodeRecordCodec::BYTES,
           (unsigned long)_store.fileBytes((uint16_t)_o.smallRecords), (unsigned long)_store.fileBytes((uint16_t)_o.fullRecords));
  _h.log(false, line);
  struct { const char* name; const TimingStats* t; } rows[] = {
      {"new small bucket", &_r.smallSave}, {"grow to full", &_r.fullSave}, {"read full", &_r.read},
      {"rewrite full", &_r.rewrite},       {"lookup 1-byte ID", &_r.lookup}, {"remove", &_r.remove},
  };
  for (auto& row : rows) {
    row.t->format(st, sizeof(st));
    snprintf(line, sizeof(line), "  %-17s %s", row.name, st);
    _h.log(false, line);
  }
  snprintf(line, sizeof(line), "  every bucket read once: %.0f ms", _r.fullReadUs / 1000.0);
  _h.log(false, line);
  if (_r.usedBefore >= 0 && _r.usedSmall >= 0 && _r.usedFull >= 0) {
    const int64_t small = _r.usedSmall - _r.usedBefore, full = _r.usedFull - _r.usedBefore;
    snprintf(line, sizeof(line),
             "  flash: %lld KB before; small buckets +%lld KB (%lld B each); full +%lld KB (%lld B each); after %lld KB",
             (long long)(_r.usedBefore / 1024), (long long)(small / 1024), (long long)(small / _o.buckets),
             (long long)(full / 1024), (long long)(full / _o.buckets), (long long)(_r.usedAfter / 1024));
    _h.log(false, line);
  }
  snprintf(line, sizeof(line), "  longest step %.1f ms, whole bench %.1f s", _r.longestStepUs / 1000.0,
           _r.elapsedUs / 1e6);
  _h.log(false, line);
  if (_r.ok) _h.log(false, "RESULT ok");
  else {
    snprintf(line, sizeof(line), "RESULT FAILED at %s: %s (storage failures %lu)", phaseName(_r.failedIn), _r.failure,
             (unsigned long)_store.stats().failures);
    _h.log(true, line);
  }
}
