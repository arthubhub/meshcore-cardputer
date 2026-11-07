#pragma once
// Copies the packet log to a CSV file on the SD card (RADIO-6). Off by default and forgotten at restart. Short, spaced
// writes under sdcard::Lock, because the card shares its SPI bus with the radio.
#include <stddef.h>
#include <stdint.h>

class PacketLog;

class PacketSdLogger {
public:
  static constexpr const char* PATH = "/cardputer_packets.csv";
  static constexpr uint32_t WRITE_EVERY_MS = 5000;
  static constexpr uint32_t MAX_FILE_BYTES = 16u * 1024 * 1024;

  bool start(const PacketLog& log);
  void stop(const PacketLog& log);          // writes what is still waiting
  bool active() const { return _active; }
  void loop(const PacketLog& log, uint32_t nowMs);

private:
  void write(const PacketLog& log);
  bool _active = false;
  uint32_t _next = 0;       // next packet to write
  uint32_t _lastWrite = 0;
  uint32_t _bytes = 0;
  uint32_t _lost = 0;
};
