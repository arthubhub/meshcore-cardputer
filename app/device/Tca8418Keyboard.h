#pragma once
// Cardputer ADV keyboard: TCA8418 on the internal I2C bus (0x34, SDA 8 / SCL 9, INT 11).
// Thin Arduino wrapper around the portable driver core/drivers/Tca8418 (tested on PC against a
// fake chip). I2C goes through M5.In_I2C (shared with the codec and the Cap IO expander).
#include "../core/drivers/Tca8418.h"
#include <CpadvHal.h>

class Tca8418Keyboard {
public:
  static constexpr int PIN_INT = 11;
  Tca8418Keyboard();
  bool begin();
  bool poll(KeyEvent& out);
  bool ok() const { return _drv.ok(); }
  uint32_t eventsRead() const { return _drv.eventsRead(); }
  uint32_t i2cErrors() const { return _drv.i2cErrors(); }
  bool intLevel() { return _int.level(); }
  void setRawEcho(bool on) { _drv.setRawEcho(on); }

private:
  GpioIn _int;
  Tca8418 _drv;
};
