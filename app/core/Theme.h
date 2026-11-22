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

#include "drivers/Palette.h"

namespace theme {

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int STATUS_H = 12;           // top status bar
constexpr int HELP_H = 12;             // bottom key-hint line
constexpr int CONTENT_Y = STATUS_H;
constexpr int CONTENT_H = SCREEN_H - STATUS_H - HELP_H;
constexpr int LINE_H = 13;             // one line of the 6x13 font
constexpr int MARGIN = 3;

// Colours of the theme in use (custom themes): read like constants by the screens, set by apply() from a palette of
// core/drivers/Palette.h (built-in themes, theme files from the SD card). They start with Dark, the original colours.
extern uint16_t BG, FG, DIM, ACCENT, COLOR_OK, COLOR_WARN, COLOR_BAD, BAR_BG, SELECT_BG, BUBBLE_IN, BUBBLE_OUT,
    MENTION_BG,      // "@name" in a bubble (MESSAGING-13)
    MENTION_ME_BG;   // "@me", drawn with BG-coloured text
void apply(const Palette& p);
const char* applied();   // the name of the theme in use

// Latin-1 fonts (accents français). Defined in Fonts.cpp.
extern const lgfx::U8g2font& fontSmall;   // 6x10, status bar and hints
extern const lgfx::U8g2font& fontText;    // 6x13, body text
extern const lgfx::U8g2font& fontBold;    // 6x13 bold, titles

}  // namespace theme

// Copies `src` (UTF-8) into `out`, truncated with ".." so that it fits in `maxWidth` pixels
// with the canvas' current font. Never splits a UTF-8 sequence.
void fitText(lgfx::LovyanGFX& g, const char* src, int maxWidth, char* out, size_t outSize);
