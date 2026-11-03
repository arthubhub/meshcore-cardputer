#include "LittleFsStorage.h"
#include <CpadvLog.h>
#include <Arduino.h>
#include <LittleFS.h>

bool LittleFsStorage::read(const char* path, uint32_t off, void* buf, uint32_t len) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  bool ok = f.seek(off) && f.read((uint8_t*)buf, len) == len;
  f.close();
  return ok;
}

bool LittleFsStorage::write(const char* path, uint32_t off, const void* buf, uint32_t len) {
  // "r+" keeps the content (in-place update of one record); "w" creates a missing file.
  File f = LittleFS.exists(path) ? LittleFS.open(path, "r+") : LittleFS.open(path, "w");
  if (!f) {
    CPLOG_I("FS", "storage: cannot open %s", path);
    return false;
  }
  if (off > f.size()) {   // extend with zeros up to the offset (ring files grow slot by slot)
    f.seek(f.size());
    static const uint8_t zeros[32] = {};
    uint32_t gap = off - f.size();
    while (gap) {
      uint32_t n = gap > sizeof(zeros) ? sizeof(zeros) : gap;
      if (f.write(zeros, n) != n) break;
      gap -= n;
    }
  }
  bool ok = f.seek(off) && f.write((const uint8_t*)buf, len) == len;
  f.close();
  if (!ok) CPLOG_I("FS", "storage: write failed %s @%u (%u bytes)", path, (unsigned)off, (unsigned)len);
  return ok;
}

bool LittleFsStorage::remove(const char* path) {
  return LittleFS.exists(path) && LittleFS.remove(path);
}
