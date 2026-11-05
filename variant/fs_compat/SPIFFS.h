#pragma once
// SPIFFS for the dual-image companion = the shared LittleFS data partition (VALIDATION-8).
//
// MeshCore's ESP32 companion (meshcore/examples/companion_radio: main.cpp, DataStore.cpp, MyMesh.h) stores its
// files through the global `SPIFFS` object and mounts it with SPIFFS.begin(true), which formats the partition
// when it is not SPIFFS. In the dual image that partition is the keyboard firmware's LittleFS, so each mode
// switch erased the other mode's data. MeshCore is never modified (CLAUDE.md), so the dual companion env puts
// this folder first on the include path and ignores the framework SPIFFS library (platformio.ini):
// `#include <SPIFFS.h>` then gives this header, where fs::SPIFFSFS is a cpadv::DataFs - the very class the
// keyboard firmware uses. Same file system, same files, same identity in both modes.
//
// Only the API MeshCore uses is needed: begin(), format(), totalBytes(), usedBytes() and the fs::FS methods;
// DataFs (a LittleFSFS) provides all of them. CardputerBoard.cpp checks at compile time that this header is the
// one in use in the dual companion.
#include <CpadvDataFs.h>

#define CPADV_SPIFFS_IS_DATAFS 1

namespace fs {

class SPIFFSFS : public cpadv::DataFs {
public:
  // Same signature as the framework's SPIFFSFS::begin. The mount point and label are those of the data
  // partition (DataFs::MOUNT_POINT / PARTITION): MeshCore passes neither.
  bool begin(bool formatOnFail = false, const char* basePath = nullptr, uint8_t maxOpenFiles = 10,
             const char* partitionLabel = nullptr) {
    (void)basePath;
    (void)maxOpenFiles;
    (void)partitionLabel;
    return mount(formatOnFail, "companion");
  }
};

}  // namespace fs

extern fs::SPIFFSFS SPIFFS;
