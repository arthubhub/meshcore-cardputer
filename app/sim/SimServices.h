#pragma once
// Fake implementations of the UI services for the PC simulator.
// Everything is driven by the scenario script (see main_sim.cpp), with a virtual clock
// so that screenshots are reproducible. Messages are stored with the real MessageStore,
// on host files, so persistence and reboots are tested for real.
#include "../core/Services.h"
#include "../core/DemoMesh.h"
#include <deque>
#include <string>
#include <vector>

// MessageStore backend on host files (one directory per scenario).
class SimStorage : public Storage {
public:
  explicit SimStorage(std::string dir) : _dir(std::move(dir)) {}
  bool read(const char* path, uint32_t off, void* buf, uint32_t len) override;
  bool write(const char* path, uint32_t off, const void* buf, uint32_t len) override;
  bool remove(const char* path) override;
  uint32_t writes = 0;

private:
  std::string full(const char* path) const;
  std::string _dir;
};

class SimGps;

class SimSystem : public SystemService {
public:
  uint32_t virtualMillis = 0;
  DeviceStatus st;
  bool soundOn = true;
  GpsService* gps = nullptr;

  SimSystem() { st.batteryPercent = 82; }
  uint32_t millis() override { return virtualMillis; }
  DeviceStatus status() override;
  uint32_t freeHeap() override { return 180 * 1024; }
  uint32_t minFreeHeap() override { return 171 * 1024; }
  uint16_t batteryMilliVolts() override { return 3600 + st.batteryPercent * 6; }
  const char* firmwareVersion() override { return "sim-dev"; }
  void beep(uint16_t freqHz, uint16_t durationMs) override;
  void playSound(SoundKind kind) override;

  uint32_t nowUnix() { return st.unixTime ? st.unixTime + virtualMillis / 1000 : 0; }
  int sounds = 0;

  // Dual-boot image simulation ("dual on|off", "expect-companion-boots n")
  bool dualBoot = false;
  int companionBoots = 0;
  bool companionInstalled() override { return dualBoot; }
  void requestCompanionBoot() override { companionBoots++; printf("[SIM] restart into the companion firmware requested\n"); }
};

// GPS fed with NMEA sentences by the scenario (same parser as on the device).
class SimGps : public GpsService {
public:
  explicit SimGps(SimSystem& sys) : _sys(sys) {}
  bool feed(const std::string& sentence) { _present = true; _quick = false; return _parser.feedLine(sentence.c_str(), _sys.millis()); }
  void quick(const std::string& mode, int sats);   // "fix" (at the scenario position), "nofix", "none"
  const GpsState& state() override { return _quick ? _q : _parser.state(); }
  bool present() override { return _present; }
  uint32_t baud() override { return _present ? 115200 : 0; }
  void coldRestart() override { _parser.reset(); _quick = false; printf("[SIM] GPS cold restart\n"); }

private:
  SimSystem& _sys;
  NmeaParser _parser;
  GpsState _q;
  bool _quick = false;
  bool _present = false;
};

// Map tiles from a host folder (same layout as the SD card: <dir>/tiles/<z>/<x>/<y>.bin).
class SimTiles : public TileSource {
public:
  bool open(const std::string& dir);   // reads <dir>/tiles/info.txt
  bool available() override { return _ok; }
  bool zoomRange(int& zmin, int& zmax) override { zmin = _zmin; zmax = _zmax; return _ok; }
  bool hint(double& lat, double& lon) override { lat = _lat; lon = _lon; return _ok; }
  bool beginTile(int z, int x, int y) override;
  bool readRow(int row, int x0, int w, uint16_t* dst) override;
  void endTile() override;
  int tilesRead = 0, rowsRead = 0;

private:
  std::string _dir;
  bool _ok = false;
  int _zmin = 0, _zmax = 0;
  double _lat = 0, _lon = 0;
  FILE* _f = nullptr;
};

class SimSettings : public SettingsService {
public:
  UiSettings s;
  int saves = 0;
  const UiSettings& get() override { return s; }
  bool save(const UiSettings& n) override;
};

// "SD card" backup: keeps a copy of the settings (the device also copies identity, contacts, channels).
class SimBackup : public BackupService {
public:
  explicit SimBackup(SimSettings& settings) : _settings(settings) {}
  bool card = false;
  int restores = 0;
  bool available() override { return card; }
  bool exportConfig(char* msg, size_t n) override;
  bool importConfig(char* msg, size_t n) override;

private:
  SimSettings& _settings;
  bool _has = false;
  UiSettings _saved;
};

// The fake network now lives in core/DemoMesh (also used by the Cardputer demo firmware).
using SimMesh = DemoMesh;

