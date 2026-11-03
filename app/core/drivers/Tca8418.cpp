#include "Tca8418.h"
#include "../Log.h"

bool Tca8418::rd(uint8_t reg, uint8_t& v) {
  if (_bus.readReg(ADDR, reg, v)) { _consecutiveErrors = 0; return true; }
  _i2cErrors++;
  _consecutiveErrors++;
  return false;
}

bool Tca8418::wr(uint8_t reg, uint8_t v) {
  if (_bus.writeReg(ADDR, reg, v)) { _consecutiveErrors = 0; return true; }
  _i2cErrors++;
  _consecutiveErrors++;
  return false;
}

bool Tca8418::begin() {
  _ok = false;
  _pendingCount = _pendingPos = 0;
  _matrix.reset();
  if (!_bus.probe(ADDR)) {
    CORE_LOGE("KEYS", "TCA8418 (I2C 0x34) does not answer: keyboard disabled");
    return false;
  }
  // Same configuration as M5Stack's driver (Adafruit_TCA8418 begin + matrix(7, 8)):
  // all pins inputs, 7 rows x 8 columns in keypad mode, key event interrupt only.
  bool ok = true;
  for (uint8_t i = 0; i < 3; i++) {
    ok &= wr(REG_GPIO_DIR_1 + i, 0x00);
    ok &= wr(REG_GPI_EM_1 + i, 0xFF);
    ok &= wr(REG_GPIO_INT_EN_1 + i, 0x00);
  }
  ok &= wr(REG_KP_GPIO_1, 0x7F);   // ROW0..ROW6
  ok &= wr(REG_KP_GPIO_2, 0xFF);   // COL0..COL7
  ok &= wr(REG_KP_GPIO_3, 0x00);
  ok &= wr(REG_CFG, CFG_KE_IEN);

  // Read the configuration back: a chip that acknowledges but ignores writes is a dead keyboard.
  uint8_t cfg = 0, kp1 = 0, kp2 = 0;
  ok &= rd(REG_CFG, cfg) && rd(REG_KP_GPIO_1, kp1) && rd(REG_KP_GPIO_2, kp2);
  if (ok && ((cfg & CFG_KE_IEN) == 0 || kp1 != 0x7F || kp2 != 0xFF)) {
    CORE_LOGE("KEYS", "TCA8418 config read-back mismatch: CFG 0x%02X KP1 0x%02X KP2 0x%02X", cfg, kp1, kp2);
    ok = false;
  }

  // Drop whatever was pressed during boot
  int flushed = 0;
  uint8_t ev = 0;
  while (flushed < 16 && rd(REG_KEY_EVENT_A, ev) && ev) flushed++;
  wr(REG_INT_STAT, INT_ALL);

  _ok = ok;
  if (ok) CORE_LOGI("KEYS", "TCA8418 keyboard ready (7x8 matrix, %d boot events dropped, INT %s)", flushed,
                    _int ? (_int->level() ? "high (idle)" : "LOW (stuck?)") : "not used");
  else CORE_LOGE("KEYS", "TCA8418 keyboard NOT configured (%lu I2C errors)", (unsigned long)_i2cErrors);
  return ok;
}

// Reads the event FIFO into _pending. Returns true if events were fetched.
bool Tca8418::fetch(uint32_t nowMs) {
  bool intActive = _int && !_int->level();   // active low
  if (!intActive && nowMs - _lastPoll < SLOW_POLL_MS) return false;
  _lastPoll = nowMs;

  uint8_t count = 0;
  if (!rd(REG_KEY_LCK_EC, count)) return false;
  count &= 0x0F;
  if (count == 0) {
    if (intActive) {   // interrupt without event (GPI, lock...): acknowledge it or INT stays low
      uint8_t st = 0;
      if (rd(REG_INT_STAT, st) && (st & INT_OVR_FLOW)) _overflows++;
      wr(REG_INT_STAT, INT_ALL);
    }
    return false;
  }
  _pendingCount = _pendingPos = 0;
  for (uint8_t i = 0; i < count && i < sizeof(_pending); i++) {
    uint8_t ev = 0;
    if (!rd(REG_KEY_EVENT_A, ev) || ev == 0) break;
    _pending[_pendingCount++] = ev;
    _events++;
  }
  uint8_t st = 0;
  if (rd(REG_INT_STAT, st) && (st & INT_OVR_FLOW)) {
    _overflows++;
    CORE_LOGW("KEYS", "TCA8418 event FIFO overflow (keys lost)");
  }
  wr(REG_INT_STAT, INT_ALL);
  return _pendingCount > 0;
}

// A brown-out or a loose contact resets the chip: it then answers but no longer scans the keypad.
bool Tca8418::healthy(uint32_t nowMs) {
  if (nowMs - _lastHealth < HEALTH_MS) return true;
  _lastHealth = nowMs;
  uint8_t cfg = 0, kp1 = 0;
  if (!rd(REG_CFG, cfg) || !rd(REG_KP_GPIO_1, kp1)) return true;   // bus errors: handled by poll()
  if ((cfg & CFG_KE_IEN) && kp1 == 0x7F) return true;
  CORE_LOGW("KEYS", "TCA8418 lost its configuration (reset / power glitch?), re-initialising");
  _reinits++;
  _lastRetry = nowMs;
  begin();
  return _ok;
}

bool Tca8418::poll(KeyEvent& out, uint32_t nowMs) {
  if (_ok && !healthy(nowMs)) return false;
  if (!_ok) {
    // Chip absent or crashed: retry from time to time (loose connector, brown-out...)
    if (nowMs - _lastRetry >= RETRY_MS) {
      _lastRetry = nowMs;
      if (_bus.probe(ADDR)) {
        CORE_LOGW("KEYS", "TCA8418 answers again, re-initialising");
        begin();
      }
    }
    if (!_ok) return false;
  }
  for (;;) {
    while (_pendingPos < _pendingCount) {
      uint8_t ev = _pending[_pendingPos++];
      bool got = _matrix.feed(ev, out);
      if (_rawEcho) {
        int row = -1, col = -1;
        KeyMatrix::toGrid(ev & 0x7F, row, col);
        CORE_LOGI("KEYS", "raw 0x%02X key %u %s -> r%d c%d -> %s code %d ch '%c' mods 0x%02X", ev, ev & 0x7F,
                  (ev & 0x80) ? "down" : "up", row, col, got ? "KEY" : "-", got ? (int)out.code : -1,
                  got && out.ch >= 32 ? out.ch : ' ', _matrix.mods());
      }
      if (got) return true;
    }
    if (!fetch(nowMs)) {
      if (_consecutiveErrors >= 10) {
        CORE_LOGE("KEYS", "TCA8418 stopped answering (%lu I2C errors), will retry", (unsigned long)_i2cErrors);
        _ok = false;
        _lastRetry = nowMs;
      }
      return false;
    }
  }
}
