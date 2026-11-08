#if !CPADV_DEMO   // radio only: the demo firmware has no MeshCore mesh
#include "PacketSdLogger.h"
#include "SdCard.h"
#include "../core/drivers/PacketLog.h"
#include <Arduino.h>
#include <CpadvLog.h>
#include <SD.h>

bool PacketSdLogger::start(const PacketLog& log) {
  if (_active) return true;
  if (!sdcard::mounted()) return false;
  sdcard::Lock lock;
  File f = SD.open(PATH, FILE_APPEND);
  if (!f) {
    CPLOG_E("PKT", "cannot open %s on the SD card", PATH);
    return false;
  }
  _bytes = f.size();
  if (_bytes == 0) _bytes += f.println(PacketLog::csvHeader());
  f.close();
  _next = log.total();
  _lastWrite = millis();
  _lost = 0;
  _active = true;
  CPLOG_I("PKT", "packet capture on: %s (%u KB so far)", PATH, (unsigned)(_bytes / 1024));
  return true;
}

void PacketSdLogger::stop(const PacketLog& log) {
  if (!_active) return;
  write(log);
  _active = false;
  CPLOG_I("PKT", "packet capture off: %u KB, %u packets lost", (unsigned)(_bytes / 1024), (unsigned)_lost);
}

void PacketSdLogger::loop(const PacketLog& log, uint32_t nowMs) {
  if (!_active || nowMs - _lastWrite < WRITE_EVERY_MS) return;
  _lastWrite = nowMs;
  if (log.total() < _next) _next = 0;            // the log was cleared
  if (log.total() != _next) write(log);
}

void PacketSdLogger::write(const PacketLog& log) {
  if (log.total() == _next) return;
  sdcard::Lock lock;
  File f = SD.open(PATH, FILE_APPEND);
  if (!f) {
    CPLOG_E("PKT", "SD write failed, packet capture stopped");
    _active = false;
    return;
  }
  char line[PacketLog::CSV_LINE_MAX];
  for (; _next < log.total(); _next++) {
    const PacketRecord* r = log.bySeq(_next);
    if (!r) { _lost++; continue; }               // more packets than the ring holds in one interval
    PacketLog::formatCsv(*r, line, sizeof(line));
    _bytes += f.println(line);
  }
  f.close();
  if (_bytes >= MAX_FILE_BYTES) {
    CPLOG_W("PKT", "%s is full (%u MB), packet capture stopped", PATH, (unsigned)(_bytes >> 20));
    _active = false;
  }
}

#endif  // !CPADV_DEMO
