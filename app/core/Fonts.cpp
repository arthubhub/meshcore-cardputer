#include "Theme.h"
#include "FontData.h"
#include <string.h>

namespace theme {
static const lgfx::U8g2font s_small(u8g2_font_6x10_tf);
static const lgfx::U8g2font s_text(u8g2_font_6x13_tf);
static const lgfx::U8g2font s_bold(u8g2_font_6x13B_tf);

const lgfx::U8g2font& fontSmall = s_small;
const lgfx::U8g2font& fontText = s_text;
const lgfx::U8g2font& fontBold = s_bold;
}  // namespace theme

void fitText(lgfx::LovyanGFX& g, const char* src, int maxWidth, char* out, size_t outSize) {
  if (outSize == 0) return;
  size_t n = strlen(src);
  if (n >= outSize) n = outSize - 1;
  // Never cut in the middle of a UTF-8 sequence when clamping to the buffer.
  while (n > 0 && (src[n] & 0xC0) == 0x80) n--;
  memcpy(out, src, n);
  out[n] = 0;
  if (g.textWidth(out) <= maxWidth) return;

  const int dotsW = g.textWidth("..");
  while (n > 0) {
    n--;
    while (n > 0 && (src[n] & 0xC0) == 0x80) n--;   // step back one code point
    out[n] = 0;
    if (g.textWidth(out) + dotsW <= maxWidth) break;
  }
  if (n + 3 <= outSize) {
    out[n] = '.'; out[n + 1] = '.'; out[n + 2] = 0;
  }
}
