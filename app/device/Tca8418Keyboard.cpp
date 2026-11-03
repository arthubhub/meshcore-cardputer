#include "Tca8418Keyboard.h"
#include <Arduino.h>

Tca8418Keyboard::Tca8418Keyboard() : _int(PIN_INT), _drv(internalI2c(), &_int) {}

bool Tca8418Keyboard::begin() {
  _int.begin(true);   // open-drain, active low; M5's board has a pull-up, ours is a harmless spare
  return _drv.begin();
}

bool Tca8418Keyboard::poll(KeyEvent& out) { return _drv.poll(out, millis()); }
