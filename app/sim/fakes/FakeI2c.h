#pragma once
// Fake I2C bus for the simulator and the unit tests: routes register accesses to fake chips.
#include "../../core/hal/Hal.h"
#include <map>
#include <vector>

class FakeI2cDevice {
public:
  virtual ~FakeI2cDevice() {}
  virtual bool present() { return true; }
  virtual bool read(uint8_t reg, uint8_t& v) = 0;
  virtual bool write(uint8_t reg, uint8_t v) = 0;
};

class FakeI2cBus : public I2cBus {
public:
  void attach(uint8_t addr, FakeI2cDevice* dev) { _devs[addr] = dev; }
  bool probe(uint8_t addr) override {
    auto it = _devs.find(addr);
    return it != _devs.end() && it->second->present();
  }
  bool readReg(uint8_t addr, uint8_t reg, uint8_t& v) override {
    reads++;
    return probe(addr) && _devs[addr]->read(reg, v);
  }
  bool writeReg(uint8_t addr, uint8_t reg, uint8_t v) override {
    writes++;
    return probe(addr) && _devs[addr]->write(reg, v);
  }
  unsigned reads = 0, writes = 0;

private:
  std::map<uint8_t, FakeI2cDevice*> _devs;
};

// Model of the TCA8418 keypad scanner (only what the driver uses): registers, 10-event FIFO,
// KEY_LCK_EC counter, INT_STAT bits, active-low INT line, overflow.
class FakeTca8418 : public FakeI2cDevice, public DigitalIn {
public:
  FakeTca8418() { regs.assign(0x30, 0); }

  // --- faults to inject
  bool absent = false;        // no ACK at all
  bool ignoreWrites = false;  // ACKs but registers stay at reset values (dead / wrong chip)
  int failNextReads = 0;      // transient I2C read errors
  bool intWired = true;       // false: INT line always reads high (not connected)

  // --- keyboard actions (physical TCA8418 row 0..6 / column 0..7)
  void pressRaw(int tcaRow, int tcaCol, bool down) { push((uint8_t)((tcaRow * 10 + tcaCol + 1) | (down ? 0x80 : 0))); }
  // Physical Cardputer key, row 0..3 / col 0..13 (KeyMatrix grid)
  bool pressGrid(int row, int col, bool down);
  void tap(int row, int col) { pressGrid(row, col, true); pressGrid(row, col, false); }

  // --- DigitalIn (INT pin)
  bool level() override { return !intWired || (regs[0x02] & regs[0x01] & 0x1F & 0x01) == 0; }

  // --- FakeI2cDevice
  bool present() override { return !absent; }
  bool read(uint8_t reg, uint8_t& v) override;
  bool write(uint8_t reg, uint8_t v) override;

  bool configured() const { return regs[0x1D] == 0x7F && regs[0x1E] == 0xFF; }
  std::vector<uint8_t> fifo;
  std::vector<uint8_t> regs;
  unsigned dropped = 0;

private:
  void push(uint8_t ev);
};

// Model of the PI4IOE5V6408 I/O expander (Cap LoRa-1262).
class FakePi4ioe : public FakeI2cDevice {
public:
  FakePi4ioe() { reset(); }
  void reset() { id = 0xA2; dir = 0x00; out = 0x00; hiz = 0xFF; writeLog.clear(); }
  bool absent = false;
  bool readsFail = false;
  uint8_t id, dir, out, hiz;
  std::vector<std::pair<uint8_t, uint8_t>> writeLog;   // (register, value) in order

  bool present() override { return !absent; }
  bool read(uint8_t reg, uint8_t& v) override;
  bool write(uint8_t reg, uint8_t v) override;
  // true if pin is driven (output, not high-Z) at the given level
  bool drives(int pin, bool high) const {
    uint8_t b = 1 << pin;
    return (dir & b) && !(hiz & b) && (((out & b) != 0) == high);
  }
};
