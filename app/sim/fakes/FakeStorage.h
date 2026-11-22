#pragma once
// A file system in memory for the portable stores (BucketStore, MessageStore, ContactFile), which can lose power in
// the middle of any operation that changes it. After the cut every operation fails, like a device that is off, until
// reboot(). The model is the pessimistic one: a write cut short keeps its first bytes (a FAT card does that; LittleFS
// keeps the whole previous file instead), a remove or a rename either happens or not. With setAtomic(true) it models
// LittleFS instead: writeFile() replaces a file whole or not at all, and says so (atomicWriteFile()).
// Directories: with requireDirs, a file can only be created in a directory made by makeDir() (like LittleFS).
#include "../../core/hal/Storage.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class FakeStorage : public Storage {
public:
  bool read(const char* path, uint32_t off, void* buf, uint32_t len) override;
  bool write(const char* path, uint32_t off, const void* buf, uint32_t len) override;
  bool remove(const char* path) override;
  bool rename(const char* from, const char* to) override;
  bool writeFile(const char* path, const void* a, uint32_t alen, const void* b, uint32_t blen) override;
  bool atomicWriteFile() const override { return _atomic; }
  bool makeDir(const char* path) override;

  void setAtomic(bool on) { _atomic = on; }
  void setRequireDirs(bool on) { _requireDirs = on; }
  std::set<std::string> dirs;

  // The power goes during the `op`-th change from now (0 = the next write, remove or rename). A write cut that way
  // keeps its first `keepBytes` bytes.
  void cutAt(uint32_t op, uint32_t keepBytes = 0);
  void reboot();                       // power back, nothing pending
  bool powerLost() const { return _off; }
  // Every write fails from now on, without a cut (a full partition); reads, removes and renames still work.
  void failWrites(bool on) { _failWrites = on; }

  bool exists(const std::string& path) const { return files.count(path) > 0; }
  // Changes done since the start, cut ones included: lets a test try a cut at every step of an operation
  uint32_t changes() const { return _changes; }

  std::map<std::string, std::vector<uint8_t>> files;
  uint32_t reads = 0, writes = 0, removes = 0, renames = 0;

private:
  bool change();                       // counts one change; false when the power is (or just went) off

  bool parentExists(const std::string& path) const;
  bool _off = false, _failWrites = false, _atomic = false, _requireDirs = false;
  bool _armed = false;
  uint32_t _cutIn = 0, _keep = 0;
  uint32_t _changes = 0;
  bool _cutting = false;               // the current change is the one cut
};
