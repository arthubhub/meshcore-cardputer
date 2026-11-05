#include "DeviceServices.h"
#include <CpadvLog.h>
#include "../core/RadioPresets.h"
#include <Arduino.h>
#include <stddef.h>
#include <CpadvDataFs.h>   // the data partition (shared with phone mode)
#include <M5Unified.h>
#include <target.h>
#include <CpadvBoot.h>


namespace {
constexpr const char* SETTINGS_PATH = "/cpadv_settings.bin";
constexpr uint32_t SETTINGS_MAGIC = 0x43504144;   // "CPAD"
constexpr uint16_t SETTINGS_VERSION = 1;

struct SettingsFile {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  UiSettings s;
  uint32_t checksum;
};

uint32_t checksum(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261u;   // FNV-1a
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}

const RadioPreset& presetOf(const UiSettings& s) {
  return RADIO_PRESETS[s.radioPreset < RADIO_PRESET_COUNT ? s.radioPreset : 0];
}
}  // namespace

// ------------------------------------------------------------------ DeviceSettings
// File layout: magic(4) version(2) size(2) payload(size) [pad to 4] checksum(4).
// When UiSettings grows, older files still load: the new fields keep their defaults.

static uint32_t align4(uint32_t v) { return (v + 3) & ~3u; }

void DeviceSettings::load() {
  File f = cpadv::dataFs().openIfExists(SETTINGS_PATH);
  if (!f) {
    CPLOG_I("SET", "settings: no file, using defaults");
    return;
  }
  uint8_t buf[sizeof(SettingsFile) + 64] = {};
  size_t n = f.read(buf, sizeof(buf));
  f.close();
  uint32_t magic;
  uint16_t version, size;
  memcpy(&magic, buf, 4);
  memcpy(&version, buf + 4, 2);
  memcpy(&size, buf + 6, 2);
  uint32_t csOff = align4(8 + size), cs = 0;
  if (n < csOff + 4 || magic != SETTINGS_MAGIC || version != SETTINGS_VERSION || size > sizeof(UiSettings)) {
    CPLOG_I("SET", "settings: file invalid, using defaults");
    return;
  }
  memcpy(&cs, buf + csOff, 4);
  if (cs != checksum(buf + 8, size)) {
    CPLOG_I("SET", "settings: checksum mismatch, using defaults");
    return;
  }
  UiSettings s;                  // defaults for fields the file does not have yet
  memcpy(&s, buf + 8, size);
  _s = s;
  _s.nodeName[sizeof(_s.nodeName) - 1] = 0;
  CPLOG_I("SET", "settings loaded (%u/%u bytes): name='%s' preset=%u tx=%d share=%d", size,
                (unsigned)sizeof(UiSettings), _s.nodeName, _s.radioPreset, _s.txPowerDbm, _s.shareLocation);
}

bool DeviceSettings::save(const UiSettings& s) {
  bool radioChanged = s.radioPreset != _s.radioPreset || s.txPowerDbm != _s.txPowerDbm;
  _s = s;
  SettingsFile sf;
  sf.magic = SETTINGS_MAGIC;
  sf.version = SETTINGS_VERSION;
  sf.size = sizeof(UiSettings);
  sf.s = _s;
  sf.checksum = checksum((const uint8_t*)&sf.s, sizeof(sf.s));
  static_assert(offsetof(SettingsFile, s) == 8, "settings payload must start at offset 8");
  File f = cpadv::dataFs().open(SETTINGS_PATH, "w");
  bool ok = f && f.write((const uint8_t*)&sf, sizeof(sf)) == sizeof(sf);
  if (f) f.close();
  CPLOG_I("SET", "settings saved (%s)", ok ? "ok" : "WRITE FAILED");
  if (radioChanged) applyRadio();
  return ok;
}

void DeviceSettings::applyRadio() {
  if (!_radioReady) return;
  const RadioPreset& p = presetOf(_s);
  radio_driver.setParams(p.freqMHz, p.bwKHz, p.sf, p.cr);
  radio_driver.setTxPower(_s.txPowerDbm);
  CPLOG_I("SET", "radio set: %.3f MHz BW %.1f SF %u CR %u, %d dBm", p.freqMHz, p.bwKHz, p.sf, p.cr,
                _s.txPowerDbm);
}

// ------------------------------------------------------------------ DeviceSystem

uint32_t DeviceSystem::millis() { return ::millis(); }

bool DeviceSystem::companionInstalled() {
  return cpadv::dualBootImage() && cpadv::runningSlot() == cpadv::Slot::Standalone;
}

void DeviceSystem::requestCompanionBoot() {
  CPLOG_I("BOOT", "phone mode requested from the settings: restart in 1.5 s");
  _companionAt = ::millis() + 1500;   // let the notification show and the radio finish
  if (!_companionAt) _companionAt = 1;
}

DeviceStatus DeviceSystem::status() {
  DeviceStatus st;
  st.batteryPercent = M5.Power.getBatteryLevel();
  st.charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  const GpsState& g = _gps.state();
  st.gpsFix = g.valid;
  st.gpsSatellites = g.sats;
  if (g.valid) {
    st.hasPosition = true;
    st.lat = g.lat;
    st.lon = g.lon;
  }
  st.bleEnabled = _settings.get().ble;
  st.bleConnected = false;
  uint32_t t = rtc_clock.getCurrentTime();
  st.unixTime = t;   // the UI shows --:-- until the clock is valid (GPS, contacts' adverts)
  return st;
}

uint32_t DeviceSystem::freeHeap() { return ESP.getFreeHeap(); }
uint32_t DeviceSystem::minFreeHeap() { return ESP.getMinFreeHeap(); }
uint16_t DeviceSystem::batteryMilliVolts() { return board.getBattMilliVolts(); }

void DeviceSystem::beep(uint16_t freqHz, uint16_t durationMs) {
  if (!_settings.get().sound) return;
  M5.Speaker.tone(freqHz, durationMs);
}

void DeviceSystem::playSound(SoundKind kind) {
  if (!_settings.get().sound) return;
  // Queued on one channel (stop_current_sound = false): the notes play one after the other.
  switch (kind) {
    case SOUND_DIRECT_MSG:
      M5.Speaker.tone(2200, 70, 0, true);
      M5.Speaker.tone(2900, 90, 0, false);
      break;
    case SOUND_CHANNEL_MSG:
      M5.Speaker.tone(2400, 60, 0, true);
      break;
    case SOUND_SENT:
      M5.Speaker.tone(3200, 25, 0, true);
      break;
    case SOUND_FAIL:
      M5.Speaker.tone(700, 120, 0, true);
      M5.Speaker.tone(450, 180, 0, false);
      break;
  }
}
