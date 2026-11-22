#pragma once
// Device bench of the node archive's storage (MESSAGING-12, step 3; docs/storage/NODES.md, "Unknowns"). Measures what
// the design rests on, on the real flash: time to write, read and rewrite a bucket, the flash taken by small and full
// buckets, and the time of a full read (search, boot count).
//
// A state machine: step() does at most one storage operation, so the owner chooses where it runs - between two
// passes of loop() (the radio is served after each operation: the time of one step is the pause it causes) or in a
// task of its own. Portable: the unit tests run it on FakeStorage.
#include "BucketStore.h"
#include "NodeArchive.h"
#include <stdint.h>

class Storage;

// Durations in microseconds: count, min, average, 95th percentile, max
class TimingStats {
public:
  static constexpr int MAX_SAMPLES = 256;   // beyond, min/avg/max stay exact, the percentile uses the first ones
  void add(uint32_t us);
  int count() const { return _n; }
  uint32_t min() const { return _n ? _min : 0; }
  uint32_t max() const { return _max; }
  uint32_t avg() const { return _n ? (uint32_t)(_sum / (uint64_t)_n) : 0; }
  uint32_t percentile(int pct) const;
  uint64_t total() const { return _sum; }
  // "n 128, min 1.2, avg 3.4, p95 9.8, max 12.0 ms"
  void format(char* out, size_t n) const;

private:
  uint32_t _s[MAX_SAMPLES];
  int _n = 0;
  uint32_t _min = 0, _max = 0;
  uint64_t _sum = 0;
};

class StoreBenchHost {
public:
  virtual ~StoreBenchHost() {}
  virtual uint32_t micros() = 0;
  virtual int64_t fsUsedBytes() = 0;        // -1 if unknown
  virtual void log(bool error, const char* line) = 0;
};

class StoreBench {
public:
  struct Options {
    const char* prefix = "/nbt";          // files of the bench only; removed at the end
    uint8_t dirs = 0;                     // BucketStore::Layout::dirs (then prefix is a directory)
    int buckets = NodeArchive::BUCKETS;
    int smallRecords = 4;                 // a young archive: does LittleFS keep such files inside its directory?
    int fullRecords = NodeArchive::PER_BUCKET;
    int lookups = 256;                    // findByHash through NodeArchive (bucket read + decode)
    uint32_t seed = 0x5eed;
  };

  enum class Phase : uint8_t { Start, SmallFill, Fill, Read, Rewrite, Lookup, Clear, Done, Failed };

  struct Result {
    TimingStats smallSave, fullSave, read, rewrite, lookup, remove;
    int64_t usedBefore = -1, usedSmall = -1, usedFull = -1, usedAfter = -1;   // bytes
    uint32_t fullReadUs = 0;              // the whole Read phase: every bucket once
    uint32_t elapsedUs = 0;
    uint32_t longestStepUs = 0;
    bool ok = false;
    Phase failedIn = Phase::Start;
    char failure[64] = "";
  };

  StoreBench(Storage& storage, StoreBenchHost& host, const Options& o);
  // One operation. False when the bench is over (Done or Failed): read result() then.
  bool step();
  Phase phase() const { return _phase; }
  const Result& result() const { return _r; }
  static const char* phaseName(Phase p);
  // The report, a line at a time, through the host's log
  void report();

private:
  void fill(uint8_t* buf, int bucket, int records);
  void next(Phase p);
  bool fail(const char* what);
  uint32_t rnd();

  Storage& _st;
  StoreBenchHost& _h;
  Options _o;
  BucketStore _store;
  NodeArchive _archive;                   // for the lookups: same layout as the bench's buckets
  Phase _phase = Phase::Start;
  int _i = 0;                             // the bucket (or lookup) of the current phase
  uint32_t _seed;
  uint32_t _startUs = 0, _readStartUs = 0;
  Result _r;
  uint8_t _buf[NodeArchive::PER_BUCKET * NodeRecordCodec::BYTES];
};
