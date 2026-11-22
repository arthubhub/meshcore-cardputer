#include "SdThemes.h"
#include "SdCard.h"
#include <CpadvLog.h>
#include <SD.h>
#include <string.h>

void SdThemes::scan() {
  _scanned = true;
  _n = 0;
  if (!sdcard::mounted()) return;
  sdcard::Lock lock;
  File dir = SD.open("/themes");
  if (!dir || !dir.isDirectory()) return;      // no themes folder: nothing to do, no message
  int seen = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char* base = strrchr(f.name(), '/');
    base = base ? base + 1 : f.name();
    const size_t len = strlen(base);
    const bool txt = !f.isDirectory() && len > 4 && len - 4 < sizeof(_files[0]) && !strcasecmp(base + len - 4, ".txt");
    f.close();
    if (!txt) continue;
    seen++;
    if (_n >= ThemeCatalog::MAX_CUSTOM) continue;
    // Name order: insertion into the sorted list
    int at = _n;
    while (at > 0 && strcasecmp(_files[at - 1], base) > 0) at--;
    for (int k = _n; k > at; k--) memcpy(_files[k], _files[k - 1], sizeof(_files[0]));
    snprintf(_files[at], sizeof(_files[0]), "%s", base);
    _n++;
  }
  dir.close();
  if (seen > _n) CPLOG_W("THEME", "%d theme files in /themes, only the first %d are read", seen, _n);
}

int SdThemes::count() {
  if (!_scanned) scan();
  return _n;
}

bool SdThemes::read(int i, char* name, size_t nameLen, char* text, size_t cap) {
  if (i < 0 || i >= count() || !cap) return false;
  snprintf(name, nameLen, "%.*s", (int)(strlen(_files[i]) - 4), _files[i]);   // without ".txt"
  char path[48];
  snprintf(path, sizeof(path), "/themes/%s", _files[i]);
  sdcard::Lock lock;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  const size_t n = f.read((uint8_t*)text, cap - 1);
  f.close();
  text[n] = 0;
  return true;
}
