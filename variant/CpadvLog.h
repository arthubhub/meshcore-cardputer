#pragma once
// Serial logging for the Cardputer ADV (standalone and companion firmwares).
//
// Every line looks like:   [CPADV] 12.345 I BOARD  message
//                          prefix  uptime  level  tag
// - USB-CDC (native USB) never blocks: without a PC the lines are dropped, not waited for.
// - The first lines (boot) are kept in RAM and replayed when a serial monitor connects later,
//   so the boot log is visible even if the monitor is opened after power-on.
// - Level set at build time with -D CPADV_LOG_LEVEL=1..4 (error, warning, info, debug). Default 3.
#include <Arduino.h>

#ifndef CPADV_LOG_LEVEL
  #define CPADV_LOG_LEVEL 3
#endif

void cpadvLogf(char level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

#define CPLOG_E(tag, fmt, ...) do { if (CPADV_LOG_LEVEL >= 1) cpadvLogf('E', tag, fmt, ##__VA_ARGS__); } while (0)
#define CPLOG_W(tag, fmt, ...) do { if (CPADV_LOG_LEVEL >= 2) cpadvLogf('W', tag, fmt, ##__VA_ARGS__); } while (0)
#define CPLOG_I(tag, fmt, ...) do { if (CPADV_LOG_LEVEL >= 3) cpadvLogf('I', tag, fmt, ##__VA_ARGS__); } while (0)
#define CPLOG_D(tag, fmt, ...) do { if (CPADV_LOG_LEVEL >= 4) cpadvLogf('D', tag, fmt, ##__VA_ARGS__); } while (0)

namespace cpadv {

// Starts Serial (if needed), waits up to `waitHostMs` for a serial monitor, prints the banner.
void logBegin(const char* firmwareName, uint32_t waitHostMs = 1500);

// Call from loop(): replays the boot log when a monitor connects. Cheap.
void logLoop();

// Prints the boot banner again (also used by the "status" console command).
void printBanner();

// Lists the devices answering on the internal I2C bus (SDA 8 / SCL 9, via M5.In_I2C).
// Expected on the Cardputer ADV: 0x18 ES8311 audio, 0x34 TCA8418 keyboard, 0x43 PI4IOE (Cap LoRa-1262).
int scanInternalI2c();

// Raw line output (console replies), no prefix.
void logRaw(const char* line);

const char* resetReasonName();
const char* runningPartition();

}  // namespace cpadv
