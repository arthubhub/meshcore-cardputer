#include "CardputerBoard.h"
#include <M5Unified.h>
#include "CpadvLog.h"
#include "CpadvHal.h"
#include "../app/core/drivers/Pi4ioe.h"
#include "../app/core/drivers/Tca8418.h"
#include "CpadvBoot.h"
#include "CpadvRadioMonitor.h"

#if defined(CPADV_DUAL) && defined(DISPLAY_CLASS)
  // Dual-image companion: MeshCore's SPIFFS must be the shared LittleFS (VALIDATION-8). If the framework's
  // SPIFFS.h were picked instead, phone mode would format the keyboard firmware's data at every switch.
  #include <SPIFFS.h>
  #include <type_traits>
  #include "CpadvDataFs.h"
static_assert(std::is_base_of<cpadv::DataFs, fs::SPIFFSFS>::value,
              "dual companion: SPIFFS must come from variant/fs_compat (see platformio.ini, VALIDATION-8)");
#endif

// Companion firmware of a dual-boot image: the keyboard is only used to go back to the
// standalone firmware (esc pressed 3 times within 3 s). The standalone firmware has its own driver.
static GpioIn kbInt(11);
static Tca8418* returnKeys = nullptr;
static uint8_t escCount = 0;
static uint32_t firstEscAt = 0;

#ifndef CPADV_FW_NAME
  #define CPADV_FW_NAME "MeshCore Cardputer ADV"
#endif

// PI4IOE5V6408 IO expander on the Cap LoRa-1262 (internal I2C bus, SDA 8 / SCL 9): its P0 enables
// the Cap's RF antenna switch. Driver: app/core/drivers/Pi4ioe (tested on PC).
static constexpr uint8_t PI4IOE_CAP_PIN = 0;

#ifndef CPADV_CAP_POWER_PIN
  // Some Cap revisions are reported to need GPIO46 high before the expander answers.
  // Only driven if the expander does not answer at first.
  #define CPADV_CAP_POWER_PIN 46
#endif

void CardputerBoard::begin() {
  cpadv::logBegin(CPADV_FW_NAME);
  CPLOG_I("BOARD", "board begin");

  // The SD card shares the radio SPI bus (SCK 40 / MOSI 14 / MISO 39): keep its CS released
  // so an inserted card never answers to radio traffic.
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  ESP32Board::begin();   // Wire on the Grove port (PIN_BOARD_SDA / PIN_BOARD_SCL)

  auto cfg = M5.config();
  cfg.serial_baudrate = 0;      // Serial already started by main
  cfg.clear_display = true;
  cfg.output_power = false;
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  cfg.internal_mic = false;
  cfg.internal_spk = true;
  cfg.external_imu = false;
  cfg.external_rtc = false;
  cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  M5.begin(cfg);

  CPLOG_I("BOARD", "M5Unified board id=%d (ADV=%d), display %dx%d, internal I2C SDA %d SCL %d",
          (int)M5.getBoard(), (int)m5::board_t::board_M5CardputerADV,
          (int)M5.Display.width(), (int)M5.Display.height(), (int)M5.In_I2C.getSDA(), (int)M5.In_I2C.getSCL());
  if (M5.getBoard() != m5::board_t::board_M5CardputerADV) {
    CPLOG_W("BOARD", "board not detected as Cardputer ADV (M5Unified too old?)");
  }
  cpadv::scanInternalI2c();

  _cap1262 = probeCapExpander();
#if CPADV_CAP_POWER_PIN >= 0
  if (!_cap1262) {
    CPLOG_W("CAP", "PI4IOE not found, driving GPIO%d high and retrying", CPADV_CAP_POWER_PIN);
    pinMode(CPADV_CAP_POWER_PIN, OUTPUT);
    digitalWrite(CPADV_CAP_POWER_PIN, HIGH);
    delay(100);
    _cap1262 = probeCapExpander();
  }
#endif
  if (_cap1262) {
    CPLOG_I("CAP", "Cap LoRa-1262 detected (PI4IOE @0x43)");
    if (!enableCapRfPath()) CPLOG_E("CAP", "could not configure PI4IOE P0 (antenna switch)");
  } else {
    CPLOG_W("CAP", "no PI4IOE: assuming Cap LoRa868 (DIO2 RF switch only) or no Cap at all");
  }

  CPLOG_I("BOARD", "battery %u mV, level %d %%", getBattMilliVolts(), (int)M5.Power.getBatteryLevel());
}

bool CardputerBoard::probeCapExpander() {
  Pi4ioe io(internalI2c());
  return io.probe();
}

bool CardputerBoard::enableCapRfPath() {
  Pi4ioe io(internalI2c());
  bool ok = io.setOutput(PI4IOE_CAP_PIN, true);
  delay(5);
  return ok;
}

uint16_t CardputerBoard::getBattMilliVolts() {
  int mv = M5.Power.getBatteryVoltage();   // ADC on GPIO10, divider handled by M5Unified
  return mv > 0 ? (uint16_t)mv : 0;
}

void CardputerBoard::onBootComplete() {
  CPLOG_I("BOOT", "boot complete in %lu ms, heap free %lu", (unsigned long)millis(), (unsigned long)ESP.getFreeHeap());
  if (cpadv::dualBootImage() && cpadv::runningSlot() == cpadv::Slot::Companion) {
    static Tca8418 keys(internalI2c(), &kbInt);
    kbInt.begin(true);
    if (keys.begin()) returnKeys = &keys;
    CPLOG_I("BOOT", "phone mode: press esc (top-left key) 3 times to go back to the keyboard firmware");
  }
}

static void pollReturnKeys() {
  KeyEvent e;
  uint32_t now = millis();
  while (returnKeys->poll(e, now)) {
    bool esc = e.code == KeyCode::Esc || (e.code == KeyCode::Char && (e.ch == '`' || e.ch == '~'));
    if (!esc) { escCount = 0; continue; }
    if (escCount == 0 || now - firstEscAt > 3000) { escCount = 0; firstEscAt = now; }
    escCount++;
    CPLOG_I("BOOT", "esc %u/3", escCount);
    if (escCount >= 3) {
      M5.Display.wakeup();
      M5.Display.setBrightness(96);
      M5.Display.fillScreen(TFT_BLACK);
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Display.setTextSize(2);
      M5.Display.setCursor(8, 50);
      M5.Display.print("Keyboard mode...");
      M5.Display.setCursor(8, 74);
      M5.Display.print("Mode clavier...");
      cpadv::rebootInto(cpadv::Slot::Standalone);
      escCount = 0;
    }
  }
}

void CardputerBoard::loop() {
  cpadv::logLoop();
  if (returnKeys) pollReturnKeys();
  uint32_t now = millis();
  if (_radioMonitor) _radioMonitor->loop(now);
  if (now - _lastHeartbeat < 10000) return;
  _lastHeartbeat = now;
  if (_heartbeat) {
    _heartbeat();
    return;
  }
  char radio[120] = "radio not started";
  if (_radioMonitor) _radioMonitor->summary(radio, sizeof(radio), now);
  CPLOG_I("BEAT", "up %lus | heap %lu min %lu | battery %u mV | %s", (unsigned long)(now / 1000),
          (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(), getBattMilliVolts(), radio);
}
