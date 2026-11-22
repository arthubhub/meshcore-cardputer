#pragma once
// Every node heard, beyond MeshCore's contact table (MESSAGING-12, docs/storage/NODES.md). Portable: it only knows
// Storage, through BucketStore.
//
// - 128 buckets by the first byte of the key (key[0] >> 1), 42 records each: 5,376 nodes. The ID of a hop (1 to 3
//   bytes of a key) or the byte that names the sender of a direct message reads one bucket; there is no index in RAM.
// - upsert() merges what was just heard into the stored record, and writes only what matters (see REFRESH_S).
// - A full bucket drops its weakest record that is not protected (favourite, in the contact table, kept).
// - RAM: one bucket buffer (4 KB) and a count per bucket. Single-threaded: call it from one task only.
#include "BucketStore.h"
#include "NodeRecord.h"

class Storage;

class NodeVisitor {
public:
  virtual ~NodeVisitor() {}
  virtual bool visit(const NodeRecord& r) = 0;   // false stops the scan
};

class NodeArchive {
public:
  static constexpr int BUCKETS = 128;
  static constexpr int PER_BUCKET = 42;         // one 4 KB flash block per bucket file
  static constexpr int CAPACITY = BUCKETS * PER_BUCKET;
  static constexpr uint32_t MAGIC = 0x3142444e;  // "NDB1"
  // A node heard again within this time, with nothing new but its time and signal, is not written: flash wear
  static constexpr uint32_t REFRESH_S = 3600;
  static constexpr uint8_t ALL_TYPES = 0xFF;

  enum class Result : uint8_t {
    Added,       // a new node
    Replaced,    // a new node, which took the place of the weakest one of its bucket
    Updated,     // written
    Unchanged,   // nothing worth a write
    Refused,     // new, but its bucket is full of protected or stronger nodes
    Failed,      // the storage failed: the previous content stays
  };

  // `dirs`: subdirectories for the bucket files (BucketStore::Layout::dirs); the firmware uses "/nodes" with DIRS
  static constexpr uint8_t DIRS = 8;
  explicit NodeArchive(Storage& storage, const char* prefix = "/nb", uint8_t dirs = 0);

  static uint16_t bucketOf(const uint8_t* key) { return key[0] >> 1; }
  // The bucket files of an archive under `prefix` (the bench writes them directly)
  static BucketStore::Layout layout(const char* prefix, uint8_t dirs = 0);
  static uint8_t typeBit(uint8_t type) { return type < 8 ? (uint8_t)(1u << type) : 0; }
  static const char* resultName(Result r);

  // `heard`: what was just learnt. lastHeard is now (our clock), 0 for an import (with the Imported flag). The flags
  // Favourite / InContacts / Keep of `heard` are ignored: setFlags() changes them. When a node had to leave the bucket,
  // `dropped` (if given) receives it.
  Result upsert(const NodeRecord& heard, NodeRecord* dropped = nullptr);
  // One change to a node, for applyBatch(): a hearing (upsert), flags only, or a removal
  struct Op {
    enum Kind : uint8_t { Heard, Flags, Remove };
    Kind kind = Heard;
    const NodeRecord* record = nullptr;   // Heard: what was heard; Flags/Remove: only its key is used
    uint8_t set = 0, clear = 0;           // flags applied after the hearing (Heard and Flags)
  };
  // Several changes to nodes of one bucket (bucketOf() of each key the same) with at most one write: what the write
  // queue (NodeBook) flushes. results[i] for each: Flags/Remove on an unknown node give Refused. False when the keys are
  // not of one bucket or the storage failed (then every result that needed the write says Failed).
  bool applyBatch(const Op* ops, int n, Result* results, NodeRecord* dropped = nullptr);
  // The merge rules of upsert(): folds `heard` into `stored`; true when the change is worth a flash write.
  static bool merge(NodeRecord& stored, const NodeRecord& heard);
  // The record upsert() stores for a node not known yet
  static NodeRecord fresh(const NodeRecord& heard);
  bool get(const uint8_t* key, NodeRecord& out);
  // The nodes whose key starts with `hash` (1 to 32 bytes) and whose type is in `typeMask` (typeBit()). Fills up to
  // `max` of them and returns how many match, which may be more.
  int findByHash(const uint8_t* hash, int bytes, uint8_t typeMask, NodeRecord* out, int max);
  // Sets then clears flags of a stored node. False when the node is unknown or the write failed.
  bool setFlags(const uint8_t* key, uint8_t set, uint8_t clear);
  bool remove(const uint8_t* key);
  // Visits the records of `buckets` buckets from `fromBucket` on. Returns the bucket to start the next slice from, or
  // -1 when every bucket was visited or the visitor stopped.
  int scan(int fromBucket, int buckets, NodeVisitor& visitor);
  // Nodes stored, -1 until every bucket has been read once (a full scan)
  int knownCount() const;

  const BucketStore::Stats& storeStats() const { return _store.stats(); }
  // The buffered bucket no longer matches the flash (its files were removed behind the archive's back)
  void forgetBuffer();

private:
  bool load(uint16_t b);                // into the buffer; false when the storage failed
  bool save();                          // the buffer's bucket
  int indexOf(const uint8_t* key) const;
  NodeRecord at(int i) const;
  void put(int i, const NodeRecord& r);
  int weakest() const;                  // the weakest unprotected record of the buffer, -1 if none
  static bool weaker(const NodeRecord& a, const NodeRecord& b);
  // upsert() on the loaded buffer, without writing; `dirty` becomes true when the buffer changed
  Result apply(const NodeRecord& heard, NodeRecord* dropped, bool& dirty);
  Result applyFlags(const uint8_t* key, uint8_t set, uint8_t clear, bool& dirty);
  Result applyRemove(const uint8_t* key, bool& dirty);
  static void keyText(const uint8_t* key, char* out);   // "a1b2c3", for the log

  BucketStore _store;
  uint8_t _buf[PER_BUCKET * NodeRecordCodec::BYTES];
  int _bucket = -1;                     // held in the buffer, as on flash; -1: none
  uint16_t _count = 0;
  int8_t _counts[BUCKETS];              // records per bucket, -1 never read
};
