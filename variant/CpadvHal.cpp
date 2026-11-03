#include "CpadvHal.h"
#include <M5Unified.h>

static constexpr uint32_t FREQ = 400000;

bool M5InternalI2c::probe(uint8_t addr) { return M5.In_I2C.scanID(addr, FREQ); }

bool M5InternalI2c::readReg(uint8_t addr, uint8_t reg, uint8_t& value) {
  return M5.In_I2C.readRegister(addr, reg, &value, 1, FREQ);
}

bool M5InternalI2c::writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  return M5.In_I2C.writeRegister8(addr, reg, value, FREQ);
}

M5InternalI2c& internalI2c() {
  static M5InternalI2c bus;
  return bus;
}
