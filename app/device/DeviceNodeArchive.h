#pragma once
// The node archive on the Cardputer (MESSAGING-12; docs/storage/NODES.md): bucket files on the data partition
// (/nodes/0 .. /nodes/7), the RAM queue, and the storage task that writes the queue out.
//
// The task runs on core 0 at a low priority: LittleFS's work (10 to 300 ms per write) happens beside the radio's
// loop() on core 1, which then only pauses for the flash's own erase and program time (53 ms at worst at 240 MHz,
// 99 ms at 80 MHz, bench store task=1). Two FreeRTOS mutexes: the queue's (microseconds) and the archive's (one bucket
// read or write).
#include "../core/drivers/NodeBook.h"
#include "../core/hal/Lock.h"
#include "LittleFsStorage.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class FreeRtosLock : public Lock {
public:
  void lock() override;
  void unlock() override;

private:
  SemaphoreHandle_t _m = nullptr;     // made at the first use: after the scheduler started
};

class DeviceNodeArchive {
public:
  static constexpr uint32_t TASK_PERIOD_MS = 100;

  DeviceNodeArchive() : _archive(_storage, "/nodes", NodeArchive::DIRS), _book(_archive, &_queueLock, &_archiveLock) {}
  bool begin();                       // starts the storage task
  NodeBook& book() { return _book; }
  // Every waiting change now (restart, mode switch, backup, low battery): blocks until written
  int flushAll() { return _book.flushAll(); }
  // "archive 1234 nodes, 3 waiting, 57 writes, 0 failed" (count unknown until a full scan)
  void summary(char* out, size_t n);
  // Visits `buckets` buckets from `from` under the archive's lock (NodeArchive::scan)
  int scanSlice(int from, int buckets, NodeVisitor& v);
  // Removes every bucket file (console `nodes wipe`); returns how many
  int wipe();

private:
  static void task(void* self);

  LittleFsStorage _storage;
  NodeArchive _archive;
  FreeRtosLock _queueLock, _archiveLock;
  NodeBook _book;
  bool _started = false;
};
