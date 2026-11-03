#include "DeviceBackup.h"
#include <CpadvLog.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include "SdCard.h"

namespace {
constexpr const char* BACKUP_PATH = "/cardputer_backup.bin";
constexpr uint32_t MAGIC = 0x4B425043;   // "CPBK"
constexpr uint16_t VERSION = 1;
// Files saved (internal flash paths). Messages are not part of the backup.
const char* const FILES[] = {"/cpadv_settings.bin", "/identity/_main.id", "/contacts.cp", "/channels.cp"};
constexpr int FILE_COUNT = sizeof(FILES) / sizeof(FILES[0]);
constexpr uint32_t MAX_FILE = 64 * 1024;

bool copy(File& from, File& to, uint32_t len) {
  uint8_t buf[256];
  while (len) {
    uint32_t n = len > sizeof(buf) ? sizeof(buf) : len;
    if (from.read(buf, n) != n || to.write(buf, n) != n) return false;
    len -= n;
  }
  return true;
}
}  // namespace

bool DeviceBackup::available() { return sdcard::mounted(); }

bool DeviceBackup::exportConfig(char* msg, size_t n) {
  if (!sdcard::mounted()) { snprintf(msg, n, "Pas de carte SD"); return false; }
  if (_flush) _flush();
  sdcard::Lock lock;
  File out = SD.open(BACKUP_PATH, FILE_WRITE);
  if (!out) { snprintf(msg, n, "Écriture impossible sur la SD"); return false; }
  out.write((const uint8_t*)&MAGIC, 4);
  out.write((const uint8_t*)&VERSION, 2);
  int saved = 0;
  bool ok = true;
  for (int i = 0; i < FILE_COUNT && ok; i++) {
    File in = LittleFS.open(FILES[i], "r");
    if (!in) continue;   // e.g. no channels joined yet
    uint8_t idx = i;
    uint32_t len = in.size();
    ok = len <= MAX_FILE && out.write(&idx, 1) == 1 && out.write((const uint8_t*)&len, 4) == 4 && copy(in, out, len);
    in.close();
    if (ok) saved++;
  }
  out.close();
  CPLOG_I("BACKUP", "backup: %d files to SD %s", saved, ok ? "ok" : "FAILED");
  if (!ok) { snprintf(msg, n, "Erreur d'écriture"); return false; }
  snprintf(msg, n, "%s (%d fichiers, avec ta clé privée)", BACKUP_PATH + 1, saved);
  return true;
}

bool DeviceBackup::importConfig(char* msg, size_t n) {
  if (!sdcard::mounted()) { snprintf(msg, n, "Pas de carte SD"); return false; }
  sdcard::Lock lock;
  File in = SD.open(BACKUP_PATH, FILE_READ);
  if (!in) { snprintf(msg, n, "Aucune sauvegarde sur la carte"); return false; }
  uint32_t magic = 0;
  uint16_t version = 0;
  if (in.read((uint8_t*)&magic, 4) != 4 || in.read((uint8_t*)&version, 2) != 2 || magic != MAGIC || version != VERSION) {
    in.close();
    snprintf(msg, n, "Fichier de sauvegarde invalide");
    return false;
  }
  int restored = 0;
  bool ok = true;
  uint8_t idx;
  while (ok && in.read(&idx, 1) == 1) {
    uint32_t len = 0;
    if (in.read((uint8_t*)&len, 4) != 4 || idx >= FILE_COUNT || len > MAX_FILE) { ok = false; break; }
    File out = LittleFS.open(FILES[idx], "w", true);   // creates /identity if needed
    ok = out && copy(in, out, len);
    if (out) out.close();
    if (ok) restored++;
  }
  in.close();
  CPLOG_I("BACKUP", "restore: %d files from SD %s", restored, ok ? "ok" : "FAILED");
  if (!ok) { snprintf(msg, n, "Sauvegarde abîmée (%d fichiers restaurés)", restored); return false; }
  _restartAt = millis() + 2500;   // let the user read the message, then reload everything
  snprintf(msg, n, "%d fichiers, redémarrage...", restored);
  return true;
}
