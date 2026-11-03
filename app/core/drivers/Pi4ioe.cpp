#include "Pi4ioe.h"
#include "../Log.h"

bool Pi4ioe::probe() {
  if (!_bus.probe(ADDR)) return false;
  if (!_bus.readReg(ADDR, REG_ID, _id)) {
    CORE_LOGW("CAP", "device at 0x43 does not answer register reads");
    return false;
  }
  bool good = (_id >> 5) == MANUFACTURER;
  CORE_LOGI("CAP", "PI4IOE id register 0x%02X (manufacturer %u, expected %u)%s", _id, _id >> 5, MANUFACTURER,
            good ? "" : " -> not a PI4IOE5V6408?");
  return good;
}

static bool setBit(I2cBus& bus, uint8_t reg, uint8_t bit, bool on) {
  uint8_t v = 0;
  if (!bus.readReg(Pi4ioe::ADDR, reg, v)) return false;
  v = on ? (v | bit) : (v & ~bit);
  return bus.writeReg(Pi4ioe::ADDR, reg, v);
}

bool Pi4ioe::setOutput(uint8_t pin, bool high) {
  const uint8_t bit = 1 << pin;
  bool ok = setBit(_bus, REG_OUT, bit, high);
  ok &= setBit(_bus, REG_DIR, bit, true);
  ok &= setBit(_bus, REG_HIZ, bit, false);
  uint8_t dir = 0, out = 0, hiz = 0;
  ok &= _bus.readReg(ADDR, REG_DIR, dir) && _bus.readReg(ADDR, REG_OUT, out) && _bus.readReg(ADDR, REG_HIZ, hiz);
  bool set = (dir & bit) && (((out & bit) != 0) == high) && !(hiz & bit);
  CORE_LOGI("CAP", "PI4IOE dir=0x%02X out=0x%02X hiz=0x%02X -> P%u %s", dir, out, hiz, pin,
            set ? (high ? "HIGH (ok)" : "LOW (ok)") : "NOT SET");
  return ok && set;
}
