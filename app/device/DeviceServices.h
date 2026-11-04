#pragma once
// Cardputer implementations of the UI services (core/Services.h).
#include "../core/Services.h"
#include <CpadvVersion.inc>   // CPADV_VERSION, generated from git by tools/pio_version.py

// Settings stored in LittleFS (/cpadv_settings.bin), applied to the radio on save.
class DeviceSettings : public SettingsService {
public:
  void load();
  void applyRadio();             // pushes preset + TX power to the SX1262
  void setRadioReady(bool ok) { _radioReady = ok; }
  const UiSettings& get() override { return _s; }
  bool save(const UiSettings& s) override;

private:
  UiSettings _s;
  bool _radioReady = false;
};

class DeviceSystem : public SystemService {
public:
  DeviceSystem(DeviceSettings& settings, GpsService& gps) : _settings(settings), _gps(gps) {}
  uint32_t millis() override;
  DeviceStatus status() override;
  uint32_t freeHeap() override;
  uint32_t minFreeHeap() override;
  uint16_t batteryMilliVolts() override;
  const char* firmwareVersion() override { return CPADV_VERSION; }
  void beep(uint16_t freqHz, uint16_t durationMs) override;
  void playSound(SoundKind kind) override;
  void yield() override { if (_yield) _yield(); }
  void setYieldHook(void (*fn)()) { _yield = fn; }
  bool companionInstalled() override;
  void requestCompanionBoot() override;
  // main loop: restart into the companion once the request is due (after the banner was drawn)
  bool companionBootDue(uint32_t now) const { return _companionAt && (int32_t)(now - _companionAt) >= 0; }

private:
  DeviceSettings& _settings;
  GpsService& _gps;
  void (*_yield)() = nullptr;
  uint32_t _companionAt = 0;
};
