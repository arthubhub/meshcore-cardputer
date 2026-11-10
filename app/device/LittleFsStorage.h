#pragma once
// MessageStore backend on the Cardputer's internal flash: the data partition, cpadv::dataFs() (LittleFS).
#include "../core/MessageStore.h"

class LittleFsStorage : public Storage {
public:
  bool read(const char* path, uint32_t off, void* buf, uint32_t len) override;
  bool write(const char* path, uint32_t off, const void* buf, uint32_t len) override;
  bool remove(const char* path) override;
  bool rename(const char* from, const char* to) override;
};
