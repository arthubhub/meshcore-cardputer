#pragma once
// SQLite trial (docs/storage): runs the schema on the device with realistic data and logs every step - success or
// failure, time, free heap, SQLite's own memory, and where the time went in the file system (DbVfsMeter) - so the
// decision rests on measures. Portable: the same code runs on a PC (app/sim/dbtest) against the same SQLite sources, and
// on the Cardputer (console command `db`, env cardputer_adv_dual_dbtest).
//
// Trial 2 (2026-11-09), after the first run on the device: every setting is written then read back and logged, every
// SQLite error is logged with its code, the build options of the library are listed, and the lowest free heap of the
// whole system is reported (the first trial sampled between transactions and missed the peaks).
#include <stddef.h>
#include <stdint.h>

struct sqlite3;
struct sqlite3_stmt;

// What the trial needs from the platform
class DbProbeHost {
public:
  virtual ~DbProbeHost() {}
  virtual uint32_t ms() = 0;
  virtual uint32_t micros() = 0;
  virtual uint32_t freeHeap() = 0;          // bytes, 0 if unknown (PC)
  virtual uint32_t largestBlock() = 0;      // largest allocatable block, 0 if unknown
  virtual uint32_t systemMinHeap() = 0;     // lowest free heap since boot, 0 if unknown
  virtual uint32_t fsUsedKB() = 0;          // file system of the database, 0 if unknown
  virtual uint32_t fsFreeKB() = 0;
  virtual void service() = 0;               // between transactions: serve the radio, or just yield in a task
  virtual void log(bool error, const char* line) = 0;
  virtual void removeFile(const char* path) = 0;
  // SQLite is initialised, the meter not installed yet: the PC harness puts a VFS of its own under it here
  virtual void onSqliteReady() {}
  // Around every file operation of SQLite (the SD card's bus lock); nothing by default
  virtual void ioEnter() {}
  virtual void ioLeave() {}
};

struct DbProbeOptions {
  const char* path = "/littlefs/dbtest.db";
  const char* schema = nullptr;             // docs/storage/schema.sql
  int contacts = 1000;
  int messages = 1000;
  int batch = 10;                           // rows per transaction ("every 10 packets")
  int cacheKB = 16;                         // PRAGMA cache_size
  int pageSize = 1024;                      // PRAGMA page_size, before the schema
  const char* journal = "truncate";         // PRAGMA journal_mode: delete, truncate, persist, memory, off
  const char* sync = "normal";              // PRAGMA synchronous: full, normal, off
  int lookasideSlot = 128, lookasideCount = 32;   // SQLite's per-connection pool: 4 KB, within the 32 KB rule
  bool keep = false;                        // keep the file afterwards
  uint32_t heapFloor = 100 * 1024;          // below this lowest free heap, the trial is a memory failure
  bool repairFileControl = true;            // DbVfsMeter: false shows the failure of the ESP32 VFS
};

struct DbProbeResult {
  bool ok = false;
  bool memoryOk = true;
  char failedStep[32] = "";
  uint32_t minFreeHeap = 0, minLargestBlock = 0;   // sampled by the trial
  uint32_t systemMinBefore = 0, systemMinAfter = 0;
  uint32_t sqlitePeak = 0;                  // bytes, SQLite's high-water mark
  uint32_t fileKB = 0, fsUsedDeltaKB = 0;
  uint32_t maxTxnMs = 0, avgTxnUs = 0;
  uint32_t maxPauseMs = 0;                  // longest time without service()
  uint32_t reopenMs = 0;
};

class DbProbe {
public:
  explicit DbProbe(DbProbeHost& host) : _h(host) {}
  bool run(const DbProbeOptions& o, DbProbeResult& r);

private:
  bool configure(const DbProbeOptions& o);
  void logBuildOptions();
  bool open(const DbProbeOptions& o, bool create);
  bool setting(const char* name, const char* value, bool mustRead);
  bool fillContacts(const DbProbeOptions& o);
  bool fillMessages(const DbProbeOptions& o);
  bool queries(const DbProbeOptions& o);
  bool checkAndReopen(const DbProbeOptions& o);
  void cleanup(const DbProbeOptions& o);

  bool exec(const char* sql);
  bool execScript(const char* sql, int& statements);   // one statement at a time: a failure names its statement
  bool prepare(const char* sql, sqlite3_stmt** st);
  bool done(sqlite3_stmt* st);               // steps to the end, resets for the next use
  bool scalar(const char* sql, int64_t& out);   // logs why when there is no value
  bool text(const char* sql, char* out, size_t n);
  bool fail(const char* what);
  void sample();                             // heap and pause bookkeeping
  void serviceNow();
  void endTransaction(uint32_t startMs);
  void stepLog(const char* name, uint32_t startMs, const char* detail);
  uint32_t random();

  DbProbeHost& _h;
  sqlite3* _db = nullptr;
  DbProbeResult* _r = nullptr;
  const char* _step = "";
  uint32_t _lastService = 0;
  uint32_t _seed = 0x1234567;
  uint64_t _txnUsTotal = 0;
  uint32_t _txns = 0;
};
