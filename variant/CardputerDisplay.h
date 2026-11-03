#pragma once
// DisplayDriver for the official companion UI (examples/companion_radio/ui-new) on the
// Cardputer ADV ST7789V2 240x135 panel, through M5GFX (initialised by M5.begin()).
// The companion UI is laid out for a 128x64 screen: it is drawn into a small 8-bit sprite
// (8 KB of RAM) and scaled x1.875 to fill the panel width.
#include <helpers/ui/DisplayDriver.h>
#include <M5Unified.h>

class CardputerDisplay : public DisplayDriver {
public:
  CardputerDisplay() : DisplayDriver(128, 64), _canvas(&M5.Display) {}
  bool begin();
  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;

private:
  M5Canvas _canvas;
  bool _isOn = false;
  ColorVal _color = 0xFFFF;
};
