#pragma once
// TCA8418 keypad scanner of the Cardputer ADV (I2C 0x34 on the internal bus, INT on GPIO 11).
// Portable driver: talks through I2cBus / DigitalIn (core/hal/Hal.h) so the same code runs on the
// Cardputer and against the fake chip of the simulator (sim/fakes/FakeTca8418).
// Raw events are decoded by KeyMatrix (layout of M5Stack's M5Cardputer library).
#include "../KeyMatrix.h"
#include "../hal/Hal.h"

class Tca8418 {
public:
  static constexpr uint8_t ADDR = 0x34;
  // Registers (TI datasheet SCPS215)
  static constexpr uint8_t REG_CFG = 0x01, REG_INT_STAT = 0x02, REG_KEY_LCK_EC = 0x03, REG_KEY_EVENT_A = 0x04;
  static constexpr uint8_t REG_GPIO_INT_EN_1 = 0x1A, REG_KP_GPIO_1 = 0x1D, REG_KP_GPIO_2 = 0x1E, REG_KP_GPIO_3 = 0x1F;
  static constexpr uint8_t REG_GPI_EM_1 = 0x20, REG_GPIO_DIR_1 = 0x23;
  static constexpr uint8_t CFG_KE_IEN = 0x01;
  static constexpr uint8_t INT_K = 0x01, INT_GPI = 0x02, INT_K_LCK = 0x04, INT_OVR_FLOW = 0x08, INT_CAD = 0x10;
  static constexpr uint8_t INT_ALL = 0x1F;
  static constexpr uint32_t SLOW_POLL_MS = 50;      // poll without INT (line not wired / stuck)
  static constexpr uint32_t RETRY_MS = 2000;        // re-initialise after I2C errors
  static constexpr uint32_t HEALTH_MS = 1000;       // check the chip kept its configuration

  // `intLine` may be null (then the chip is polled every SLOW_POLL_MS).
  Tca8418(I2cBus& bus, DigitalIn* intLine) : _bus(bus), _int(intLine) {}

  bool begin();
  // Returns true and fills `out` for the next key press. Call until it returns false.
  bool poll(KeyEvent& out, uint32_t nowMs);

  bool ok() const { return _ok; }
  uint32_t eventsRead() const { return _events; }
  uint32_t i2cErrors() const { return _i2cErrors; }
  uint32_t overflows() const { return _overflows; }
  uint32_t reinits() const { return _reinits; }
  void setRawEcho(bool on) { _rawEcho = on; }
  const KeyMatrix& matrix() const { return _matrix; }

private:
  bool rd(uint8_t reg, uint8_t& v);
  bool wr(uint8_t reg, uint8_t v);
  bool fetch(uint32_t nowMs);
  bool healthy(uint32_t nowMs);

  I2cBus& _bus;
  DigitalIn* _int;
  KeyMatrix _matrix;
  bool _ok = false;
  bool _rawEcho = false;
  uint32_t _lastPoll = 0, _lastRetry = 0, _lastHealth = 0, _reinits = 0;
  uint32_t _events = 0, _i2cErrors = 0, _overflows = 0;
  uint8_t _consecutiveErrors = 0;
  uint8_t _pending[10];
  uint8_t _pendingCount = 0, _pendingPos = 0;
};
