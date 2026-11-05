#include "SdCard.h"
#include <CpadvLog.h>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <target.h>

namespace {
SemaphoreHandle_t g_bus = nullptr;
bool g_mounted = false;
bool g_tried = false;
int g_depth = 0;   // Lock nesting (the mutex is recursive)

// The ESP32 SD driver raises CS at the end of a card command without clocking the bus again, and an SD card in
// SPI mode may keep driving MISO until it sees a clock edge with CS high. The SX1262 shares MISO: its next
// answer could then be corrupted (VALIDATION-9: keyboard mode, the only one that puts the card in SPI mode,
// failed to read most received packets). One dummy byte with every chip deselected releases the line - the
// usual practice for SD cards on a shared bus.
void releaseBus() {
  SPIClass& spi = radio_spi();
  digitalWrite(PIN_SD_CS, HIGH);
  spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  spi.transfer(0xFF);
  spi.endTransaction();
}
}  // namespace

namespace sdcard {

Lock::Lock() {
  if (!g_bus) return;
  xSemaphoreTakeRecursive(g_bus, portMAX_DELAY);
  g_depth++;
}

Lock::~Lock() {
  if (!g_bus) return;
  if (--g_depth == 0 && g_mounted) releaseBus();   // last card access done: hand MISO back to the radio
  xSemaphoreGiveRecursive(g_bus);
}

bool mounted() { return g_mounted; }

bool begin() {
  if (g_tried) return g_mounted;
  g_tried = true;
  if (!g_bus) g_bus = xSemaphoreCreateRecursiveMutex();
  Lock lock;
  // Same SPIClass as the radio, with the Cap bus pins (no-op if the radio already started it)
  radio_spi().begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  g_mounted = SD.begin(PIN_SD_CS, radio_spi(), 20000000);
  if (g_mounted) CPLOG_I("SD", "card mounted, %llu MB", SD.cardSize() / (1024ULL * 1024ULL));
  else CPLOG_I("SD", "no card (backup and map unavailable, everything else works)");
  return g_mounted;
}

}  // namespace sdcard
