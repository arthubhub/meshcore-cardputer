#include "CardputerDisplay.h"
#include "CpadvLog.h"

// Dark colour scheme (easier to read outdoors, lower backlight needs)
ColorVal UIColor::window_bkg = 0x0000;
ColorVal UIColor::title_bkg = 0x001F;
ColorVal UIColor::title_txt = 0xFFFF;
ColorVal UIColor::primary_txt = 0xFFFF;
ColorVal UIColor::secondary_txt = (18 << 11) | (36 << 5) | 18;  // mid-gray
ColorVal UIColor::warning_txt = 0xFD20;
ColorVal UIColor::popup_bkg = 0x07FF;
ColorVal UIColor::popup_txt = 0x0000;
ColorVal UIColor::corp_blue = 0x001A;

static constexpr float ZOOM = 240.0f / 128.0f;

bool CardputerDisplay::begin() {
  M5.Display.setRotation(1);
  M5.Display.setBrightness(96);
  M5.Display.fillScreen(TFT_BLACK);
  _canvas.setColorDepth(8);
  _canvas.setPsram(false);
  if (!_canvas.createSprite(width(), height())) {
    CPLOG_E("DISP", "display sprite allocation failed");
    return false;
  }
  _canvas.setTextWrap(false);
  _isOn = true;
  CPLOG_I("DISP", "display ready: panel %dx%d, UI %dx%d zoom %.3f",
                (int)M5.Display.width(), (int)M5.Display.height(), width(), height(), ZOOM);
  return true;
}

void CardputerDisplay::turnOn() {
  if (!_isOn) {
    M5.Display.wakeup();
    M5.Display.setBrightness(96);
    CPLOG_I("DISP", "screen on");
  }
  _isOn = true;
}

void CardputerDisplay::turnOff() {
  if (_isOn) {
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    CPLOG_I("DISP", "screen off");
  }
  _isOn = false;
}

void CardputerDisplay::clear() {
  _canvas.fillScreen(TFT_BLACK);
}

void CardputerDisplay::startFrame(ColorVal bkg) {
  _canvas.fillScreen(bkg);
  _canvas.setTextColor(_color = UIColor::primary_txt);
}

void CardputerDisplay::setTextSize(int sz) {
  _canvas.setTextSize(sz);
}

void CardputerDisplay::setColor(ColorVal c) {
  _canvas.setTextColor(_color = c);
}

void CardputerDisplay::setCursor(int x, int y) {
  _canvas.setCursor(x, y);
}

void CardputerDisplay::print(const char* str) {
  _canvas.print(str);
}

void CardputerDisplay::fillRect(int x, int y, int w, int h) {
  _canvas.fillRect(x, y, w, h, _color);
}

void CardputerDisplay::drawRect(int x, int y, int w, int h) {
  _canvas.drawRect(x, y, w, h, _color);
}

void CardputerDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  // MeshCore UIs pass MSB-first bitmaps (Adafruit drawBitmap convention), despite the name.
  _canvas.drawBitmap(x, y, bits, w, h, _color);
}

uint16_t CardputerDisplay::getTextWidth(const char* str) {
  return _canvas.textWidth(str);
}

void CardputerDisplay::endFrame() {
  if (!_isOn) return;
  M5.Display.startWrite();
  _canvas.pushRotateZoom(&M5.Display, M5.Display.width() / 2.0f, M5.Display.height() / 2.0f, 0, ZOOM, ZOOM);
  M5.Display.endWrite();
}
