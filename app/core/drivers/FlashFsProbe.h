#pragma once
// What the data partition holds, from its first two flash blocks (VALIDATION-8).
// Used before a LittleFS mount so that a format is explained instead of silent: a blank partition is a new
// device, a valid LittleFS that does not mount is corrupted, anything else was written by another file system
// (typically SPIFFS left by a phone-mode firmware older than v0.8). Portable: tested on the PC.
#include <stddef.h>
#include <stdint.h>

namespace flashfs {

enum class Content : uint8_t {
  Blank,      // erased flash (0xFF): new device or freshly flashed image
  LittleFs,   // a LittleFS superblock is present
  Foreign,    // data, but no LittleFS superblock (SPIFFS, or garbage)
};

// `block0` and `block1` are the first bytes (at least `len`) of the partition's first two 4 KB blocks:
// LittleFS keeps its superblock in that metadata pair, with the name "littlefs" near the start of each copy.
Content classify(const uint8_t* block0, const uint8_t* block1, size_t len);

const char* describe(Content c);

}  // namespace flashfs
