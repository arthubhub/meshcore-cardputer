#include "CpadvDataFs.h"
#include "CpadvLog.h"
#include <stdio.h>
#include <esp_partition.h>
#include <sys/stat.h>
#include <vfs_api.h>

namespace cpadv {

namespace {

// VFS layer of the framework, with an exists() that does not print an error for a missing file
// (LittleFSImpl::exists opens the file, and VFSImpl::open logs "does not exist" at error level).
class QuietVfsImpl : public VFSImpl {
public:
  bool exists(const char* path) override {
    const char* mp = mountpoint();
    if (!mp || !path) return false;
    char full[96];
    int n = snprintf(full, sizeof(full), "%s%s", mp, path);
    if (n <= 0 || n >= (int)sizeof(full)) return false;
    struct stat st;
    return stat(full, &st) == 0;
  }
};

}  // namespace

DataFs::DataFs() {
  _impl = std::make_shared<QuietVfsImpl>();   // replaces LittleFSImpl (FS::_impl is protected for this)
}

flashfs::Content DataFs::probe() {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION);
  if (!p) return flashfs::Content::Foreign;
  uint8_t b0[64], b1[64];
  if (esp_partition_read(p, 0, b0, sizeof(b0)) != ESP_OK || esp_partition_read(p, 4096, b1, sizeof(b1)) != ESP_OK) {
    return flashfs::Content::Foreign;
  }
  return flashfs::classify(b0, b1, sizeof(b0));
}

bool DataFs::mount(bool formatIfUnusable, const char* owner) {
  if (_mounted) return true;
  _content = probe();
  // Plain mount first, never formatting by itself: if it fails we decide, and log why.
  _mounted = LittleFSFS::begin(false, MOUNT_POINT, 10, PARTITION);
  if (!_mounted) {
    CPLOG_W("FS", "%s: data partition '%s' does not mount: %s", owner, PARTITION, flashfs::describe(_content));
    if (!formatIfUnusable) return false;
    _dataLost = _content != flashfs::Content::Blank;
    if (_dataLost) {
      CPLOG_E("FS", "%s: formatting the data partition - its previous content is LOST "
              "(keyboard mode: Settings > Restore from SD)", owner);
    } else {
      CPLOG_I("FS", "%s: new device, formatting the data partition", owner);
    }
    if (!format()) {
      CPLOG_E("FS", "%s: format failed", owner);
      return false;
    }
    _mounted = LittleFSFS::begin(false, MOUNT_POINT, 10, PARTITION);
    if (!_mounted) {
      CPLOG_E("FS", "%s: data partition still does not mount after format", owner);
      return false;
    }
  }
  CPLOG_I("FS", "%s: data partition mounted (LittleFS, %u / %u KB used)%s", owner, (unsigned)(usedBytes() / 1024),
          (unsigned)(totalBytes() / 1024), _dataLost ? " - FORMATTED, data lost" : "");
  return true;
}

fs::File DataFs::openIfExists(const char* path) {
  if (!exists(path)) return fs::File();
  return open(path, FILE_READ);
}

}  // namespace cpadv

// ------------------------------------------------------------------ the instance of this firmware
#if CPADV_SPIFFS_ON_LITTLEFS
  // Dual-image companion: MeshCore's global SPIFFS object is a DataFs (variant/fs_compat/SPIFFS.h).
  #include <SPIFFS.h>
cpadv::DataFs& cpadv::dataFs() { return SPIFFS; }
#else
cpadv::DataFs& cpadv::dataFs() {
  static cpadv::DataFs fs;
  return fs;
}
#endif
