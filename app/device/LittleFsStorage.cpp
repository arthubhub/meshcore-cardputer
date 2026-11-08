#include "LittleFsStorage.h"
#include <CpadvLog.h>
#include <Arduino.h>
#include <CpadvDataFs.h>   // the data partition (shared with phone mode)

bool LittleFsStorage::read(const char* path, uint32_t off, void* buf, uint32_t len) {
  // A missing file is a normal answer (e.g. MessageStore probes the ring file of a new conversation):
  // openIfExists() checks with stat(), without the framework's "does not exist" error line (VALIDATION-6).
  File f = cpadv::dataFs().openIfExists(path);
  if (!f) return false;
  bool ok = f.seek(off) && f.read((uint8_t*)buf, len) == len;
  f.close();
  return ok;
}

bool LittleFsStorage::write(const char* path, uint32_t off, const void* buf, uint32_t len) {
  // "r+" keeps the content (in-place update of one record); "w" creates a missing file.
  File f = cpadv::dataFs().exists(path) ? cpadv::dataFs().open(path, "r+") : cpadv::dataFs().open(path, "w");
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

bool LittleFsStorage::rename(const char* from, const char* to) {
  return cpadv::dataFs().exists(from) && cpadv::dataFs().rename(from, to);
}

bool LittleFsStorage::remove(const char* path) {
  return cpadv::dataFs().exists(path) && cpadv::dataFs().remove(path);
}
