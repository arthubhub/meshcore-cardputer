#include <Arduino.h>
#include "target.h"
#include "CpadvLog.h"

CardputerBoard board;

// Radio SPI: the SD card shares this bus (CS 12, kept high by CardputerBoard::begin).
// The display uses its own SPI host (SPI3), configured by M5GFX.
static SPIClass spi;
SPIClass& radio_spi() { return spi; }
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  CPLOG_I("RADIO", "SX1262 init: NSS=%d DIO1=%d RST=%d BUSY=%d SCK=%d MISO=%d MOSI=%d TCXO=%.1fV DIO2_RF_SWITCH=%d",
                P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI,
                (float)SX126X_DIO3_TCXO_VOLTAGE, (int)SX126X_DIO2_AS_RF_SWITCH);
  CPLOG_I("RADIO", "Cap variant: %s", board.hasCap1262() ? "LoRa-1262 (PI4IOE)" : "LoRa868 / unknown");
  CPLOG_I("RADIO", "RTC: %s, time %lu", rtc_clock.getCurrentTime() > 1700000000u ? "set" : "not set (no GPS/phone time yet)",
          (unsigned long)rtc_clock.getCurrentTime());

  bool ok = radio.std_init(&spi);
  if (ok) {
    CPLOG_I("RADIO", "SX1262 OK: %.3f MHz BW %.1f SF %d, TX %d dBm",
                  (float)LORA_FREQ, (float)LORA_BW, (int)LORA_SF, (int)LORA_TX_POWER);
  } else {
    CPLOG_E("RADIO", "SX1262 init failed (see 'radio init failed' code above)");
    CPLOG_E("RADIO", "hints: -2 = chip not found (Cap not plugged / wiring), -707/-706 = TCXO voltage");
  }
  return ok;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
