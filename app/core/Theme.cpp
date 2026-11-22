#include "Theme.h"
#include <stdio.h>

namespace theme {

// Dark: the original palette (also ThemeCatalog's first theme)
uint16_t BG = 0x0000, FG = 0xFFFF, DIM = 0x8410, ACCENT = 0x05FF, COLOR_OK = 0x07E0, COLOR_WARN = 0xFD20,
         COLOR_BAD = 0xF800, BAR_BG = 0x10A2, SELECT_BG = 0x2967, BUBBLE_IN = 0x3186, BUBBLE_OUT = 0x122D,
         MENTION_BG = 0x2967, MENTION_ME_BG = 0xFD20;

static char g_applied[Palette::NAME_LEN + 1] = "Dark";

void apply(const Palette& p) {
  BG = p[Palette::BG];
  FG = p[Palette::FG];
  DIM = p[Palette::DIM];
  ACCENT = p[Palette::ACCENT];
  COLOR_OK = p[Palette::OK];
  COLOR_WARN = p[Palette::WARN];
  COLOR_BAD = p[Palette::BAD];
  BAR_BG = p[Palette::BAR_BG];
  SELECT_BG = p[Palette::SELECT_BG];
  BUBBLE_IN = p[Palette::BUBBLE_IN];
  BUBBLE_OUT = p[Palette::BUBBLE_OUT];
  MENTION_BG = p[Palette::MENTION_BG];
  MENTION_ME_BG = p[Palette::MENTION_ME_BG];
  snprintf(g_applied, sizeof(g_applied), "%s", p.name);
}

const char* applied() { return g_applied; }

}  // namespace theme
