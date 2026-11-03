#pragma once
// Small drawing helpers shared by the screens.
#include "Theme.h"
#include "MessageStore.h"
#include <stddef.h>

constexpr int MSG_WRAP_BUF = 200;   // > MSG_TEXT_MAX

// Selection + scrolling state of a vertical list.
struct ListState {
  int sel = 0;
  int top = 0;
  // Moves the selection by `delta` (wraps around) and keeps it visible.
  void move(int delta, int count, int visible);
  void clamp(int count, int visible);
};

constexpr int LIST_VISIBLE_ROWS = theme::CONTENT_H / theme::LINE_H;   // 8

// One list row: label on the left, value right-aligned. y = top of the row.
void drawListRow(lgfx::LovyanGFX& g, int y, const char* label, const char* value,
                 bool selected, uint16_t valueColor = theme::ACCENT, bool dimLabel = false);
void drawScrollbar(lgfx::LovyanGFX& g, int top, int visible, int count);

// UTF-8 string helpers (buffers are NUL-terminated, `cap` includes the terminator)
size_t utf8Len(const char* s);                          // number of code points
void utf8PopBack(char* s);                              // removes the last code point
bool utf8Append(char* s, size_t cap, const char* add);  // false if it does not fit

// French accent composition: Alt + base letter appends the first accented form (Alt+e: é).
// Pressing it again right away cycles through the variants (é -> è -> ê -> ë -> é).
// Returns false if `base` has no accented forms.
bool composeAccent(char* s, size_t cap, char base);

// ---- Time formatting (French) ----
// Clock considered valid once it is past 2025-01-01 (before that it was never set).
inline bool clockValid(uint32_t unix) { return unix > 1735689600u; }
void formatClock(uint32_t unix, int utcOffsetMin, char* out, size_t n);        // "14:02"
void formatAgo(uint32_t secs, char* out, size_t n);                            // "5 min", "2 h", "3 j"
// "14:02" if `unix` is the same local day as `now`, else "12/10".
void formatWhen(uint32_t unix, uint32_t now, int utcOffsetMin, char* out, size_t n);

// ---- Word wrap ----
struct TextLine { uint16_t start, len; };
// Splits UTF-8 `text` into lines no wider than `width` px with the canvas' current font.
// Breaks on spaces, or inside a word when it is wider than a line. Returns the line count.
int wrapText(lgfx::LovyanGFX& g, const char* text, int width, TextLine* lines, int maxLines);
