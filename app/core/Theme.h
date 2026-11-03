#pragma once
// Layout, colours and fonts for the 240x135 ST7789 screen.
// On the Cardputer the graphics library is M5GFX (LovyanGFX fork bundled with M5Unified);
// the simulator uses upstream LovyanGFX. Both expose the same lgfx:: API.
#if defined(ARDUINO)
  #include <M5GFX.h>
#else
  #ifndef LGFX_USE_V1
    #define LGFX_USE_V1
  #endif
  #include <LovyanGFX.hpp>
#endif

namespace theme {

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int STATUS_H = 12;           // top status bar
constexpr int HELP_H = 12;             // bottom key-hint line
constexpr int CONTENT_Y = STATUS_H;
constexpr int CONTENT_H = SCREEN_H - STATUS_H - HELP_H;
constexpr int LINE_H = 13;             // one line of the 6x13 font
constexpr int MARGIN = 3;

// RGB565 palette (dark theme, readable outdoors)
constexpr uint16_t BG        = 0x0000;
constexpr uint16_t FG        = 0xFFFF;
constexpr uint16_t DIM       = 0x8410;  // grey
constexpr uint16_t ACCENT    = 0x05FF;  // cyan-ish
constexpr uint16_t COLOR_OK        = 0x07E0;  // green
constexpr uint16_t COLOR_WARN      = 0xFD20;  // orange
constexpr uint16_t COLOR_BAD       = 0xF800;  // red
constexpr uint16_t BAR_BG    = 0x10A2;  // dark blue-grey
constexpr uint16_t SELECT_BG = 0x2967;
constexpr uint16_t BUBBLE_IN  = 0x3186;  // dark grey
constexpr uint16_t BUBBLE_OUT = 0x122D;  // dark blue

// Latin-1 fonts (accents français). Defined in Fonts.cpp.
extern const lgfx::U8g2font& fontSmall;   // 6x10, status bar and hints
extern const lgfx::U8g2font& fontText;    // 6x13, body text
extern const lgfx::U8g2font& fontBold;    // 6x13 bold, titles

}  // namespace theme

// Copies `src` (UTF-8) into `out`, truncated with ".." so that it fits in `maxWidth` pixels
// with the canvas' current font. Never splits a UTF-8 sequence.
void fitText(lgfx::LovyanGFX& g, const char* src, int maxWidth, char* out, size_t outSize);
