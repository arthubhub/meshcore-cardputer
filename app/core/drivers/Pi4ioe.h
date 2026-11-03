#pragma once
// PI4IOE5V6408 8-bit I/O expander of the Cap LoRa-1262 (I2C 0x43 on the internal bus).
// P0 drives the RF antenna switch: it must be an output at high level before the radio is used.
// Portable (I2cBus), shared by both firmwares (CardputerBoard) and tested with sim/fakes/FakePi4ioe.
#include "../hal/Hal.h"

class Pi4ioe {
public:
  static constexpr uint8_t ADDR = 0x43;
  static constexpr uint8_t REG_ID = 0x01;    // bits 7..5 = manufacturer ID (0b101), bit 0 = software reset
  static constexpr uint8_t REG_DIR = 0x03;   // 1 = output
  static constexpr uint8_t REG_OUT = 0x05;   // output level
  static constexpr uint8_t REG_HIZ = 0x07;   // 1 = output high-impedance (reset value 0xFF)
  static constexpr uint8_t REG_IN = 0x0F;    // input status
  static constexpr uint8_t MANUFACTURER = 0x5;

  explicit Pi4ioe(I2cBus& bus) : _bus(bus) {}

  // true if the chip answers with the expected manufacturer ID.
  bool probe();
  // Makes `pin` a push-pull output at `high`, then reads back and checks it. Order matters:
  // level first, then direction, then leave high-impedance (no glitch on the line).
  bool setOutput(uint8_t pin, bool high);
  uint8_t id() const { return _id; }

private:
  I2cBus& _bus;
  uint8_t _id = 0;
};
