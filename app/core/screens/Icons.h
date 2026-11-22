#pragma once
// Section icons of the navigation menu, drawn with shapes on a 16x16 grid (no bitmap, any scale, theme colours).
#include "../Theme.h"

enum class NavIcon : uint8_t { HOME, MESSAGES, CONTACTS, GPS, MAP, NEARBY, PACKETS, SETTINGS, TRACER };

// Draws `icon` with its top-left corner at (x, y), `scale` pixels per grid unit (1: 16 px, 2: 32 px)
void drawNavIcon(lgfx::LovyanGFX& g, NavIcon icon, int x, int y, int scale, uint16_t color);
