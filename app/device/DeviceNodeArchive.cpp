#include "DeviceNodeArchive.h"
#include <Arduino.h>
#include <CpadvLog.h>
#include <stdio.h>

void FreeRtosLock::lock() {
  if (!_m) {
    // Two tasks may race to make it: a critical section keeps it to one
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    SemaphoreHandle_t fresh = xSemaphoreCreateMutex();
    portENTER_CRITICAL(&mux);
    if (!_m) {
      _m = fresh;
      fresh = nullptr;
    }
    portEXIT_CRITICAL(&mux);
    if (fresh) vSemaphoreDelete(fresh);
  }
  xSemaphoreTake(_m, portMAX_DELAY);
}

void FreeRtosLock::unlock() {
  if (_m) xSemaphoreGive(_m);
}

bool DeviceNodeArchive::begin() {
  if (_started) return true;
  // 6 KB of stack: LittleFS's path walk and the batch (bench store ran its writes in such a task)
  if (xTaskCreatePinnedToCore(task, "nodes", 6144, this, 1, nullptr, 0) != pdPASS) {
    CPLOG_E("NODES", "cannot start the archive task (heap %u): changes stay in RAM", ESP.getFreeHeap());
    return false;
  }
  _started = true;
  CPLOG_I("NODES", "node archive: /nodes, %d buckets of %d, up to %d nodes; writes from a task on core 0",
          NodeArchive::BUCKETS, NodeArchive::PER_BUCKET, NodeArchive::CAPACITY);
  return true;
}

void DeviceNodeArchive::task(void* self) {
  DeviceNodeArchive* a = (DeviceNodeArchive*)self;
  for (;;) {
    const bool wrote = a->_book.flushOne(millis());
    // Crowded (a burst of new nodes): write again at once, so the queue does not overflow
    vTaskDelay(wrote && a->_book.crowded() ? 1 : pdMS_TO_TICKS(TASK_PERIOD_MS));
  }
}

void DeviceNodeArchive::summary(char* out, size_t n) {
  const NodeBook::Stats s = _book.stats();
  int known;
  {
    LockGuard g(_archiveLock);
    known = _archive.knownCount();
  }
  char count[16];
  if (known >= 0) snprintf(count, sizeof(count), "%d", known);
  else snprintf(count, sizeof(count), "?");
  snprintf(out, n, "archive %s nodes, %d waiting, %lu writes, %lu added, %lu dropped, %lu failed", count,
           _book.pending(), (unsigned long)s.writes, (unsigned long)s.added, (unsigned long)s.dropped,
           (unsigned long)s.failed);
}

int DeviceNodeArchive::scanSlice(int from, int buckets, NodeVisitor& v) {
  LockGuard g(_archiveLock);
  return _archive.scan(from, buckets, v);
}

int DeviceNodeArchive::wipe() {
  LockGuard g(_archiveLock);
  int removed = 0;
  char path[32];
  for (int b = 0; b < NodeArchive::BUCKETS; b++) {
    snprintf(path, sizeof(path), "/nodes/%x/%02x", b * NodeArchive::DIRS / NodeArchive::BUCKETS, b);
    if (_storage.remove(path)) removed++;
  }
  _archive.forgetBuffer();
  return removed;
}
