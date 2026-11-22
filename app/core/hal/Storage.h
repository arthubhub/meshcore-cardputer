#pragma once
// Byte-level file access, implemented by the platform: LittleFsStorage on the Cardputer (the data partition),
// SimStorage (host files) in the simulator, FakeStorage (memory, with power cuts) in the unit tests.
// Used by the portable stores: MessageStore, ContactFile, BucketStore.
#include <stdint.h>

class Storage {
public:
  virtual ~Storage() {}
  // Reads exactly `len` bytes at `off`. False if the file is missing or too short.
  virtual bool read(const char* path, uint32_t off, void* buf, uint32_t len) = 0;
  // Writes `len` bytes at `off`, creating / extending the file as needed.
  virtual bool write(const char* path, uint32_t off, const void* buf, uint32_t len) = 0;
  virtual bool remove(const char* path) = 0;
  // Gives `from` the name `to`; `to` must not exist. False if `from` is missing.
  virtual bool rename(const char* from, const char* to) = 0;

  // The whole file becomes `a` then `b` (`b` may be empty): an older, longer content leaves nothing behind.
  // Default: remove, then two writes - fine for any backend, but not atomic.
  virtual bool writeFile(const char* path, const void* a, uint32_t alen, const void* b, uint32_t blen) {
    remove(path);
    if (!write(path, 0, a, alen)) return false;
    return !blen || write(path, alen, b, blen);
  }
  // True when writeFile() is atomic: after a power cut the file holds the old content or the new one, never a mix.
  // LittleFS is (a file is switched to its new blocks when it is closed); FAT on an SD card is not. A store that needs
  // that guarantee does a temporary file and a rename when this is false.
  virtual bool atomicWriteFile() const { return false; }
  // Creates a directory (its parent must exist). True when it exists afterwards. Default: no directories needed.
  virtual bool makeDir(const char* path) { (void)path; return true; }
};
