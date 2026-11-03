#include "SimServices.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>

// ------------------------------------------------------------------ SimStorage

std::string SimStorage::full(const char* path) const { return _dir + path; }

bool SimStorage::read(const char* path, uint32_t off, void* buf, uint32_t len) {
  FILE* f = fopen(full(path).c_str(), "rb");
  if (!f) return false;
  bool ok = fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
  fclose(f);
  return ok;
}

bool SimStorage::write(const char* path, uint32_t off, const void* buf, uint32_t len) {
  std::string p = full(path);
  FILE* f = fopen(p.c_str(), "r+b");
  if (!f) f = fopen(p.c_str(), "w+b");
  if (!f) return false;
  bool ok = fseek(f, off, SEEK_SET) == 0 && fwrite(buf, 1, len, f) == len;
  fclose(f);
  writes++;
  return ok;
}

bool SimStorage::remove(const char* path) { return ::remove(full(path).c_str()) == 0; }

// ------------------------------------------------------------------ SimTiles

bool SimTiles::open(const std::string& dir) {
  _dir = dir;
  _ok = false;
  tilesRead = rowsRead = 0;
  FILE* f = fopen((dir + "/tiles/info.txt").c_str(), "r");
  if (!f) return false;
  _ok = fscanf(f, "%lf %lf %d %d", &_lat, &_lon, &_zmin, &_zmax) == 4;
  fclose(f);
  return _ok;
}

bool SimTiles::beginTile(int z, int x, int y) {
  char p[64];
  snprintf(p, sizeof(p), "/tiles/%d/%d/%d.bin", z, x, y);
  _f = fopen((_dir + p).c_str(), "rb");
  if (_f) tilesRead++;
  return _f != nullptr;
}

bool SimTiles::readRow(int row, int x0, int w, uint16_t* dst) {
  if (!_f || fseek(_f, (row * 256 + x0) * 2, SEEK_SET) != 0) return false;
  rowsRead++;
  return fread(dst, 2, w, _f) == (size_t)w;
}

void SimTiles::endTile() {
  if (_f) fclose(_f);
  _f = nullptr;
}

// ------------------------------------------------------------------ SimSystem / SimSettings

DeviceStatus SimSystem::status() {
  DeviceStatus s = st;
  if (s.unixTime) s.unixTime += virtualMillis / 1000;
  if (gps) {
    const GpsState& g = gps->state();
    s.gpsFix = g.valid;
    s.gpsSatellites = g.sats;
    if (g.valid) { s.hasPosition = true; s.lat = g.lat; s.lon = g.lon; }
  }
  return s;
}

void SimGps::quick(const std::string& mode, int sats) {
  _quick = true;
  _q = GpsState();
  _present = mode != "none";
  if (mode == "fix") {
    DeviceStatus st = _sys.st;   // position from the "pos" command, if any
    _q.valid = true;
    _q.sats = sats;
    _q.hdop = 0.9f;
    _q.fixType = 3;
    _q.lat = st.hasPosition ? st.lat : 43.6116;
    _q.lon = st.hasPosition ? st.lon : 1.3346;
    _q.altM = 162;
    _q.fixMillis = _sys.millis();
    _q.sentences = 10;
  } else if (mode == "nofix") {
    _q.sats = sats;
    _q.sentences = 10;
  }
}

void SimSystem::beep(uint16_t freqHz, uint16_t durationMs) {
  printf("[SIM] beep %u Hz %u ms\n", freqHz, durationMs);
}

void SimSystem::playSound(SoundKind kind) {
  static const char* const names[] = {"direct", "channel", "sent", "fail"};
  sounds++;
  printf("[SIM] sound %s\n", names[kind]);
}

bool SimSettings::save(const UiSettings& n) {
  s = n;
  saves++;
  printf("[SIM] settings saved: name='%s' preset=%u tx=%d tz=%d timeout=%u sound=%d ble=%d\n", s.nodeName,
         s.radioPreset, s.txPowerDbm, s.utcOffsetMinutes, s.screenTimeoutSecs, s.sound, s.ble);
  return true;
}

// ------------------------------------------------------------------ SimBackup

bool SimBackup::exportConfig(char* msg, size_t n) {
  if (!card) { snprintf(msg, n, "Pas de carte SD"); return false; }
  _saved = _settings.get();
  _has = true;
  snprintf(msg, n, "cardputer_backup.bin (4 fichiers)");
  printf("[SIM] backup exported\n");
  return true;
}

bool SimBackup::importConfig(char* msg, size_t n) {
  if (!card || !_has) { snprintf(msg, n, "Aucune sauvegarde sur la carte"); return false; }
  _settings.save(_saved);
  restores++;
  snprintf(msg, n, "Redémarrage dans 2 s...");
  printf("[SIM] backup restored (the device would restart)\n");
  return true;
}

