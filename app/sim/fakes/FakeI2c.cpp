#include "FakeI2c.h"
#include "../../core/KeyMatrix.h"

bool FakeTca8418::pressGrid(int row, int col, bool down) {
  uint8_t k = KeyMatrix::fromGrid(row, col);
  if (!k) return false;
  push((uint8_t)(k | (down ? 0x80 : 0)));
  return true;
}

void FakeTca8418::push(uint8_t ev) {
  if (!configured()) { dropped++; return; }   // keypad not set up: no key scanning at all
  if (fifo.size() >= 10) {
    regs[0x02] |= 0x08;   // OVR_FLOW_INT
    dropped++;
    return;
  }
  fifo.push_back(ev);
  regs[0x02] |= 0x01;   // K_INT
}

bool FakeTca8418::read(uint8_t reg, uint8_t& v) {
  if (failNextReads > 0) { failNextReads--; return false; }
  if (reg >= regs.size()) return false;
  switch (reg) {
    case 0x03: v = (uint8_t)(fifo.size() & 0x0F); return true;   // KEY_LCK_EC
    case 0x04:                                                    // KEY_EVENT_A: pops the FIFO
      if (fifo.empty()) { v = 0; return true; }
      v = fifo.front();
      fifo.erase(fifo.begin());
      return true;
    default: v = regs[reg]; return true;
  }
}

bool FakeTca8418::write(uint8_t reg, uint8_t v) {
  if (reg >= regs.size()) return false;
  if (ignoreWrites) return true;
  if (reg == 0x02) regs[reg] &= ~v;   // INT_STAT: write 1 to clear
  else regs[reg] = v;
  return true;
}

bool FakePi4ioe::read(uint8_t reg, uint8_t& v) {
  if (readsFail) return false;
  switch (reg) {
    case 0x01: v = id; return true;
    case 0x03: v = dir; return true;
    case 0x05: v = out; return true;
    case 0x07: v = hiz; return true;
    case 0x0F: v = out & dir; return true;
    default: v = 0; return true;
  }
}

bool FakePi4ioe::write(uint8_t reg, uint8_t v) {
  writeLog.push_back({reg, v});
  switch (reg) {
    case 0x01: if (v & 1) reset(); return true;
    case 0x03: dir = v; return true;
    case 0x05: out = v; return true;
    case 0x07: hiz = v; return true;
    default: return true;
  }
}
