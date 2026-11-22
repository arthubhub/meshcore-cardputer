#pragma once
// The node archive as the firmware uses it (MESSAGING-12, step 4; docs/storage/NODES.md): every change waits in RAM
// and reaches the flash one bucket at a time, from a task of its own on the Cardputer (flushOne()), so the radio's
// loop() never waits for a flash write.
//
// - heard(), markFlags(), forget(): RAM only, under the queue's lock (held for microseconds). A change to a node that
//   already waits is merged into it (NodeArchive::merge for hearings; flags accumulate; a removal replaces the rest).
// - flushOne(): picks the bucket of the oldest change when it is due (Policy), copies at most BATCH changes of that
//   bucket, and writes them in one NodeArchive::applyBatch under the archive's lock - the queue's lock is free during
//   the write. A change merged while it was being written stays queued for the next write.
// - get() / findByHash() see the waiting changes, also those being written.
// - flushAll(): before a restart, the screen going off or a low battery.
// RAM: QUEUE entries (about 2.8 KB) and a batch of BATCH copies (about 0.9 KB).
#include "../hal/Lock.h"
#include "NodeArchive.h"

class NodeBook {
public:
  static constexpr int QUEUE = 24;
  static constexpr int BATCH = 8;          // changes written at once (they share one bucket)

  struct Policy {
    uint32_t maxAgeMs = 60000;   // a change waits at most this long
    uint32_t minGapMs = 2000;    // between two writes, unless the queue reaches highWater
    int highWater = 16;          // this many waiting: write at once
  };

  struct Stats {
    uint32_t heard = 0, merged = 0;
    uint32_t dropped = 0;        // changes refused because the queue was full
    uint32_t writes = 0;         // bucket writes
    uint32_t added = 0, updated = 0, unchanged = 0, refused = 0, failed = 0;
  };

  // `queueLock` / `archiveLock`: nullptr when one task uses the book (tests, simulator)
  NodeBook(NodeArchive& archive, Lock* queueLock = nullptr, Lock* archiveLock = nullptr);
  void setPolicy(const Policy& p);

  // False when the queue is full (the change is dropped and counted; adverts come again).
  bool heard(const NodeRecord& r, uint32_t nowMs);
  bool markFlags(const uint8_t* key, uint8_t set, uint8_t clear, uint32_t nowMs);
  bool forget(const uint8_t* key, uint32_t nowMs);

  bool get(const uint8_t* key, NodeRecord& out);
  int findByHash(const uint8_t* hash, int bytes, uint8_t typeMask, NodeRecord* out, int max);

  // One bucket written when a change is due; true when a write was made. From the storage task. When the queue is
  // crowded (highWater), the bucket with the most waiting changes goes first: each write then empties more of it.
  bool flushOne(uint32_t nowMs);
  // The queue is crowded: the storage task should write again without pausing
  bool crowded();
  // Every waiting change now; returns the number of bucket writes.
  int flushAll();

  int pending();
  Stats stats();
  NodeArchive& archive() { return _a; }

private:
  struct Entry {
    NodeRecord r;                // Heard: the merged hearing; otherwise only the key
    uint32_t sinceMs = 0;
    bool used = false;
    bool hasHearing = false;     // r holds a hearing to upsert
    bool remove = false;         // forget the node (wins over the rest)
    bool writing = false;        // copied into the batch being written
    bool touched = false;        // changed while being written: stays for the next write
    uint8_t set = 0, clear = 0;
  };
  int find(const uint8_t* key) const;
  int slot(const uint8_t* key, uint32_t nowMs);   // the entry of `key`, or a new one; -1 when full
  int oldestIdle() const;
  int busiestIdle() const;          // an entry of the bucket with the most idle changes
  int countIdle() const;
  bool writeBucket(uint16_t bucket);

  NodeArchive& _a;
  NoLock _noLock;
  Lock& _ql;
  Lock& _al;
  Policy _p;
  Entry _q[QUEUE];
  NodeRecord _batch[BATCH];
  uint32_t _lastWriteMs = 0;
  bool _wrote = false;
  Stats _stats;
};
