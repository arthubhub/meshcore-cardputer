#pragma once
// Theme files of the SD card (custom themes): /themes/*.txt, read once at start (App::loadThemes). Up to
// ThemeCatalog::MAX_CUSTOM files, in name order. Every access holds the card's bus lock (sdcard::Lock).
#include "../core/Services.h"
#include "../core/drivers/Palette.h"

class SdThemes : public ThemeSource {
public:
  int count() override;
  bool read(int i, char* name, size_t nameLen, char* text, size_t cap) override;

private:
  void scan();
  bool _scanned = false;
  int _n = 0;
  char _files[ThemeCatalog::MAX_CUSTOM][32] = {};
};
