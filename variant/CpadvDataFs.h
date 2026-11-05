#pragma once
// The data partition ("spiffs" label) as LittleFS: the one file system of the Cardputer firmwares.
//
// VALIDATION-8: in the dual image both firmwares share this partition (same radio identity in both modes).
// MeshCore's ESP32 companion uses SPIFFS and the keyboard firmware LittleFS, each formatting what it could not
// mount, so every mode switch erased the other mode's data. Now both go through this class: the keyboard
// firmware directly (cpadv::dataFs()), the dual companion through the SPIFFS adapter of variant/fs_compat/,
// which makes MeshCore's `SPIFFS` object a DataFs.
//
// VALIDATION-6: LittleFS's own exists() opens the file, and the framework prints an error line for every
// missing file. DataFs answers exists() with stat(): a missing file is a normal answer, not an error.
//
// One instance per firmware (two objects mounting one partition would not work): use cpadv::dataFs().
#include <FS.h>
#include <LittleFS.h>
#include "../app/core/drivers/FlashFsProbe.h"

namespace cpadv {

class DataFs : public fs::LittleFSFS {
public:
  static constexpr const char* PARTITION = "spiffs";
  static constexpr const char* MOUNT_POINT = "/littlefs";

  DataFs();

  // Mounts the partition. When it holds no usable LittleFS and `formatIfUnusable` is true, formats it - and
  // says why (new device / corrupted / another file system): the data is lost, the SD backup can restore it.
  // `owner` names the firmware in the log ("keyboard", "companion").
  bool mount(bool formatIfUnusable, const char* owner);

  bool mounted() const { return _mounted; }
  // The partition was formatted during this boot although it was not blank: data was lost.
  bool dataLostAtBoot() const { return _dataLost; }
  flashfs::Content contentAtBoot() const { return _content; }

  // Opens `path` for reading only if it exists (no framework error line when it does not).
  fs::File openIfExists(const char* path);

private:
  flashfs::Content probe();
  bool _mounted = false;
  bool _dataLost = false;
  flashfs::Content _content = flashfs::Content::Blank;
};

// The data file system of this firmware.
DataFs& dataFs();

}  // namespace cpadv
