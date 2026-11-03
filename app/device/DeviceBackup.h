#pragma once
// Configuration backup to /cardputer_backup.bin on the SD card: settings, identity (the
// private key!), contacts and channels. Restoring rewrites them on the internal flash and
// restarts the Cardputer.
#include "../core/Services.h"

class DeviceBackup : public BackupService {
public:
  // Called before an export so that the files on flash are up to date (contacts, index...).
  void setFlushHook(void (*fn)()) { _flush = fn; }
  bool available() override;
  bool exportConfig(char* msg, size_t n) override;
  bool importConfig(char* msg, size_t n) override;
  bool restartPending() const { return _restartAt != 0; }
  uint32_t restartAt() const { return _restartAt; }

private:
  void (*_flush)() = nullptr;
  uint32_t _restartAt = 0;
};
