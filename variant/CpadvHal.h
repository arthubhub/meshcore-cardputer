#pragma once
// Arduino / M5Unified implementations of the portable hardware interfaces (core/hal/Hal.h).
#include <Arduino.h>
#include "../app/core/hal/Hal.h"

// Internal I2C bus of the Cardputer ADV (SDA 8 / SCL 9) through M5.In_I2C, 400 kHz.
class M5InternalI2c : public I2cBus {
public:
  bool probe(uint8_t addr) override;
  bool readReg(uint8_t addr, uint8_t reg, uint8_t& value) override;
  bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) override;
};

class GpioIn : public DigitalIn {
public:
  explicit GpioIn(int pin) : _pin(pin) {}
  void begin(bool pullup) { pinMode(_pin, pullup ? INPUT_PULLUP : INPUT); }
  bool level() override { return digitalRead(_pin) == HIGH; }

private:
  int _pin;
};

M5InternalI2c& internalI2c();
