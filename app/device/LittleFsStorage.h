#pragma once
// MessageStore backend on the Cardputer's internal flash (LittleFS, "spiffs" partition, 1.5 MB).
#include "../core/MessageStore.h"

class LittleFsStorage : public Storage {
public:
  bool read(const char* path, uint32_t off, void* buf, uint32_t len) override;
  bool write(const char* path, uint32_t off, const void* buf, uint32_t len) override;
  bool remove(const char* path) override;
};
