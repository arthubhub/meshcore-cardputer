#include "PositionBook.h"
#include <string.h>

int PositionBook::find(const uint8_t* key) const {
  for (int i = 0; i < _count; i++) if (memcmp(_e[i].key, key, KEY_BYTES) == 0) return i;
  return -1;
}

PositionBook::Meta PositionBook::get(const uint8_t* key) const {
  int i = find(key);
  return i < 0 ? Meta() : _e[i].meta;
}

bool PositionBook::set(const uint8_t* key, const Meta& meta) {
  int i = find(key);
  if (i < 0) {
    if (_count >= CAPACITY) return false;
    i = _count++;
    memcpy(_e[i].key, key, KEY_BYTES);
  }
  _e[i].meta = meta;
  return true;
}

void PositionBook::remove(const uint8_t* key) {
  int i = find(key);
  if (i < 0) return;
  _e[i] = _e[--_count];
}
