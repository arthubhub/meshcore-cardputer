#pragma once
// M5Stack Cardputer ADV + Cap LoRa-1262 / LoRa868.
// Display, internal I2C (keyboard, codec, Cap IO expander), speaker and battery go through
// M5Unified (vendor library, knows the ADV wiring). Wire is left to the external Grove port.
#include <Arduino.h>
#include <Wire.h>
#include <helpers/ESP32Board.h>

#ifndef PIN_SD_CS
  #define PIN_SD_CS 12
#endif

namespace cpadv { class RadioMonitor; }

class CardputerBoard : public ESP32Board {
public:
  void begin();
  // Called from loop() by both firmwares: replays the boot log when a serial monitor connects,
  // and writes a short status line every 10 s (heartbeat).
  void loop() override;
  void onBootComplete() override;

  // Replaces the default heartbeat line (the standalone firmware prints a richer one).
  void setHeartbeat(void (*fn)()) { _heartbeat = fn; }

  // Radio measurements (VALIDATION-9), attached by radio_init(); loop() drives it in both firmwares.
  void attachRadioMonitor(cpadv::RadioMonitor* m) { _radioMonitor = m; }
  cpadv::RadioMonitor* radioMonitor() const { return _radioMonitor; }

  // true when the PI4IOE5V6408 expander of the Cap LoRa-1262 answered on the internal I2C bus.
  // false = Cap LoRa868 (no expander) or no Cap at all.
  bool hasCap1262() const { return _cap1262; }

  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override { return "M5Stack Cardputer ADV"; }

private:
  bool probeCapExpander();
  bool enableCapRfPath();
  bool _cap1262 = false;
  void (*_heartbeat)() = nullptr;
  cpadv::RadioMonitor* _radioMonitor = nullptr;
  uint32_t _lastHeartbeat = 0;
};
