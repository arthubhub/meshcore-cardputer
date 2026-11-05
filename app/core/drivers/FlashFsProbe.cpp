#include "FlashFsProbe.h"
#include <string.h>

namespace flashfs {

namespace {
constexpr char MAGIC[] = "littlefs";
constexpr size_t MAGIC_LEN = sizeof(MAGIC) - 1;

bool hasMagic(const uint8_t* b, size_t len) {
  for (size_t i = 0; i + MAGIC_LEN <= len; i++) {
    if (memcmp(b + i, MAGIC, MAGIC_LEN) == 0) return true;
  }
  return false;
}

bool erased(const uint8_t* b, size_t len) {
  for (size_t i = 0; i < len; i++) if (b[i] != 0xFF) return false;
  return true;
}
}  // namespace

Content classify(const uint8_t* block0, const uint8_t* block1, size_t len) {
  if (hasMagic(block0, len) || hasMagic(block1, len)) return Content::LittleFs;
  if (erased(block0, len) && erased(block1, len)) return Content::Blank;
  return Content::Foreign;
}

const char* describe(Content c) {
  switch (c) {
    case Content::Blank: return "blank (new device)";
    case Content::LittleFs: return "LittleFS";
    case Content::Foreign: return "not LittleFS (another file system, e.g. SPIFFS from an older phone mode)";
  }
  return "?";
}

}  // namespace flashfs
