#pragma once
// Minimal hardware interfaces used by the portable drivers of core/drivers/.
// The Cardputer implements them with M5.In_I2C / Serial1 / GPIOs (device/HalArduino.*),
// the simulator and the unit tests with fake chips (sim/fakes/).
// No Arduino, no RadioLib, no MeshCore headers here.
#include <stddef.h>
#include <stdint.h>

// Register-based I2C access (8-bit register, 8-bit value).
class I2cBus {
public:
  virtual ~I2cBus() {}
  virtual bool probe(uint8_t addr) = 0;                               // device acknowledges
  virtual bool readReg(uint8_t addr, uint8_t reg, uint8_t& value) = 0;
  virtual bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) = 0;
};

// A digital input line (e.g. an active-low interrupt output).
class DigitalIn {
public:
  virtual ~DigitalIn() {}
  virtual bool level() = 0;   // true = high
};

// A UART the driver can (re)open at a given speed.
class UartPort {
public:
  virtual ~UartPort() {}
  virtual void open(uint32_t baud) = 0;
  virtual int available() = 0;
  virtual int read() = 0;                                  // -1 if nothing
  virtual size_t write(const uint8_t* data, size_t len) = 0;
};

// Wall clock the GPS keeps in sync (MeshCore RTC on the device).
class WallClock {
public:
  virtual ~WallClock() {}
  virtual uint32_t now() = 0;          // unix seconds
  virtual void set(uint32_t unix) = 0;
};
