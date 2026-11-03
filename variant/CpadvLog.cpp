#include "CpadvLog.h"
#include <M5Unified.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  #define CPADV_HWCDC 1   // Serial = USB Serial/JTAG of the ESP32-S3
#else
  #define CPADV_HWCDC 0
#endif

namespace {

// Boot log kept in RAM until a serial monitor has seen it.
constexpr size_t BOOT_LOG_SIZE = 6144;
char bootLog[BOOT_LOG_SIZE];
size_t bootLogLen = 0;
bool bootLogFull = false;
bool missedLines = false;     // something was written while no monitor was connected
bool hostWasConnected = false;
const char* fwName = "?";

SemaphoreHandle_t lock = nullptr;
StaticSemaphore_t lockBuf;

bool hostConnected() {
#if CPADV_HWCDC
  return HWCDC::isConnected();
#else
  return (bool)Serial;
#endif
}

void writeOut(const char* s, size_t n) {
  if (lock) xSemaphoreTake(lock, portMAX_DELAY);
  bool host = hostConnected();
  if (!host) missedLines = true;
  else Serial.write((const uint8_t*)s, n);
  if (!bootLogFull) {
    size_t room = BOOT_LOG_SIZE - bootLogLen;
    if (n < room) {
      memcpy(bootLog + bootLogLen, s, n);
      bootLogLen += n;
    } else {
      bootLogFull = true;   // keep the beginning (the boot), drop the rest
    }
  }
  if (lock) xSemaphoreGive(lock);
}

}  // namespace

void cpadvLogf(char level, const char* tag, const char* fmt, ...) {
  char line[256];
  uint32_t ms = millis();
  int n = snprintf(line, sizeof(line), "[CPADV] %lu.%03lu %c %-6s ", (unsigned long)(ms / 1000),
                   (unsigned long)(ms % 1000), level, tag);
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
  va_end(ap);
  size_t len = n + (m < 0 ? 0 : (size_t)m);
  if (len > sizeof(line) - 2) len = sizeof(line) - 2;
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
  line[len++] = '\n';
  writeOut(line, len);
}

namespace cpadv {

void logRaw(const char* s) {
  writeOut(s, strlen(s));
  writeOut("\n", 1);
}

const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "CRASH (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "CRASH (task watchdog)";
    case ESP_RST_WDT: return "CRASH (watchdog)";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake-up";
    case ESP_RST_BROWNOUT: return "BROWNOUT (battery low / power dip)";
    default: return "unknown";
  }
}

const char* runningPartition() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p ? p->label : "?";
}

void printBanner() {
  CPLOG_I("BOOT", "==== %s ====", fwName);
#ifdef CPADV_VERSION
  CPLOG_I("BOOT", "version %s, built %s %s", CPADV_VERSION, __DATE__, __TIME__);
#else
  CPLOG_I("BOOT", "built %s %s", __DATE__, __TIME__);
#endif
  CPLOG_I("BOOT", "reset reason: %s", resetReasonName());
  CPLOG_I("BOOT", "chip %s rev %d, %d cores, CPU %lu MHz, flash %lu KB", ESP.getChipModel(),
          (int)ESP.getChipRevision(), (int)ESP.getChipCores(), (unsigned long)getCpuFrequencyMhz(),
          (unsigned long)(ESP.getFlashChipSize() / 1024));
  const esp_partition_t* p = esp_ota_get_running_partition();
  CPLOG_I("BOOT", "running from partition '%s' at 0x%lx (%lu KB), program %lu KB", runningPartition(),
          p ? (unsigned long)p->address : 0ul, p ? (unsigned long)(p->size / 1024) : 0ul,
          (unsigned long)(ESP.getSketchSize() / 1024));
  CPLOG_I("BOOT", "heap free %lu, largest block %lu", (unsigned long)ESP.getFreeHeap(),
          (unsigned long)ESP.getMaxAllocHeap());
}

void logBegin(const char* firmwareName, uint32_t waitHostMs) {
  fwName = firmwareName;
  if (!lock) lock = xSemaphoreCreateMutexStatic(&lockBuf);
  Serial.begin(115200);   // harmless if already started
#if CPADV_HWCDC
  Serial.setTxTimeoutMs(0);   // never block when no PC is listening
#endif
  uint32_t t0 = millis();
  while (!hostConnected() && millis() - t0 < waitHostMs) delay(10);
  hostWasConnected = hostConnected();
  logRaw("");
  printBanner();
  if (!hostWasConnected) CPLOG_I("BOOT", "no serial monitor yet: this log will be replayed when one connects");
}

void logLoop() {
  bool host = hostConnected();
  if (host && !hostWasConnected) {
    hostWasConnected = true;
    if (missedLines) {
      if (lock) xSemaphoreTake(lock, portMAX_DELAY);
      Serial.write((const uint8_t*)"\n---- [CPADV] boot log replay ----\n", 35);
      Serial.write((const uint8_t*)bootLog, bootLogLen);
      if (bootLogFull) Serial.write((const uint8_t*)"(... boot log truncated)\n", 25);
      Serial.write((const uint8_t*)"---- [CPADV] end of replay ----\n", 32);
      missedLines = false;
      if (lock) xSemaphoreGive(lock);
    }
  } else if (!host && hostWasConnected) {
    hostWasConnected = false;
  }
}

int scanInternalI2c() {
  int found = 0;
  char list[160];
  size_t n = 0;
  list[0] = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (M5.In_I2C.scanID(a, 100000)) {
      found++;
      const char* what = a == 0x18 ? "ES8311 audio" : a == 0x34 ? "TCA8418 keyboard"
                       : a == 0x43 ? "PI4IOE Cap LoRa-1262" : a == 0x68 || a == 0x69 ? "IMU" : "?";
      n += snprintf(list + n, sizeof(list) - n, " 0x%02X(%s)", a, what);
      if (n >= sizeof(list)) n = sizeof(list) - 1;
    }
  }
  CPLOG_I("I2C", "internal bus (SDA 8, SCL 9): %d device(s):%s", found, found ? list : " NONE");
  if (!M5.In_I2C.scanID(0x34, 100000)) CPLOG_E("I2C", "keyboard TCA8418 (0x34) does not answer: keyboard will not work");
  return found;
}

}  // namespace cpadv

// Log sink of the portable code (app/core/Log.h)
void coreLogf(char level, const char* tag, const char* fmt, ...) {
  int lvl = level == 'E' ? 1 : level == 'W' ? 2 : level == 'I' ? 3 : 4;
  if (lvl > CPADV_LOG_LEVEL) return;
  char msg[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  cpadvLogf(level, tag, "%s", msg);
}
