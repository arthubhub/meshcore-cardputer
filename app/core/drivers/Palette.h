#pragma once
// UI themes (custom themes): the colours of the interface as a palette of named roles, the built-in themes, and the
// theme files a user adds on the SD card. Portable (no graphics library): the unit tests check every built-in theme's
// contrast. Theme.h turns the palette in use into the colour names the screens use (theme::BG, theme::FG...).
//
// Theme file (/themes/<name>.txt on the SD card), one "key = #RRGGBB" per line, '#' at the start of a line for a
// comment; keys left out come from the base theme:
//     name = Ocean            (default: the file name)
//     base = Dark             (a built-in theme; default Dark)
//     background = #001830
//     text = #E8F4FF
//     ...the other keys: see Palette::KEYS
#include <stddef.h>
#include <stdint.h>

struct Palette {
  enum Role : uint8_t {
    BG, FG, DIM, ACCENT, OK, WARN, BAD, BAR_BG, SELECT_BG, BUBBLE_IN, BUBBLE_OUT, MENTION_BG, MENTION_ME_BG, COUNT
  };
  static constexpr int NAME_LEN = 15;
  static const char* const KEYS[COUNT];     // the keys of a theme file, in Role order

  char name[NAME_LEN + 1] = "";
  uint16_t color[COUNT] = {};               // RGB565

  uint16_t operator[](Role r) const { return color[r]; }
  static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
  // WCAG contrast ratio of two RGB565 colours: 1 (same) to 21 (black on white)
  static float contrast(uint16_t a, uint16_t b);
  // The pairs the screens draw text with that are too close: fills `out` with "text on background" names, returns how
  // many. Text needs 4.5:1, dim text and accents 3:1.
  int weakPairs(const char** out, int max) const;
};

class ThemeCatalog {
public:
  static constexpr int BUILTIN = 5;         // Dark, Light, High contrast, Night, Amber
  static constexpr int MAX_CUSTOM = 8;

  ThemeCatalog();
  int count() const { return BUILTIN + _custom; }
  const Palette& at(int i) const;
  // The theme called `name`, or Dark when there is none (a theme file removed from the card)
  const Palette& find(const char* name) const;
  int indexOf(const char* name) const;      // -1 if unknown
  // The name of the theme `delta` steps from `name` in the list (wraps around)
  const char* step(const char* name, int delta) const;

  // Reads a theme file into the catalog. `fallbackName` names it when it has no "name" line (its file name).
  // Returns false (and a line in `error`) when nothing could be used; a theme with unknown keys or bad lines is still
  // added, the problems are counted in `warnings`.
  bool add(const char* text, const char* fallbackName, char* error, size_t errorLen, int& warnings);
  void clearCustom() { _custom = 0; }

  // Parses "#RRGGBB" (or "RRGGBB")
  static bool parseColor(const char* text, uint16_t& out);

private:
  Palette _builtin[BUILTIN];
  Palette _customs[MAX_CUSTOM];
  int _custom = 0;
};
