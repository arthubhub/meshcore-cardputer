#pragma once
// MessageStore backend on the Cardputer's internal flash: the data partition, cpadv::dataFs() (LittleFS).
#include "../core/MessageStore.h"

class LittleFsStorage : public Storage {
public:
  bool read(const char* path, uint32_t off, void* buf, uint32_t len) override;
  bool write(const char* path, uint32_t off, const void* buf, uint32_t len) override;
  bool remove(const char* path) override;
  bool rename(const char* from, const char* to) override;
  // One open, one or two writes, one close: LittleFS switches the file to its new blocks at the close, so a power
  // cut keeps the old content (atomicWriteFile). Each LittleFS file operation costs 10 to 300 ms (bench fsops).
  bool writeFile(const char* path, const void* a, uint32_t alen, const void* b, uint32_t blen) override;
  bool atomicWriteFile() const override { return true; }
  bool makeDir(const char* path) override;
};
