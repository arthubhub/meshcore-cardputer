#include "SdCard.h"
#include <CpadvLog.h>
#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <target.h>

namespace {
SemaphoreHandle_t g_bus = nullptr;
bool g_mounted = false;
bool g_tried = false;
}  // namespace

namespace sdcard {

Lock::Lock() { if (g_bus) xSemaphoreTakeRecursive(g_bus, portMAX_DELAY); }
Lock::~Lock() { if (g_bus) xSemaphoreGiveRecursive(g_bus); }

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
