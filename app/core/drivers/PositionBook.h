#pragma once
// When and how each contact's position was learnt (MESSAGING-11), next to the contacts themselves: MeshCore's
// ContactInfo holds a position but not its time. Keyed by the start of the public key, fixed size, no allocation.
#include <stdint.h>
#include "ContactPosition.h"

class PositionBook {
public:
  static constexpr int CAPACITY = 128;   // above the 100 contacts of the keyboard firmware
  static constexpr int KEY_BYTES = 6;

  struct Meta {
    uint32_t time = 0;
    PositionSource source = PositionSource::None;
  };

  Meta get(const uint8_t* key) const;
  // False when full: the position stays, only its time is not kept.
  bool set(const uint8_t* key, const Meta& meta);
  void remove(const uint8_t* key);
  void clear() { _count = 0; }
  int count() const { return _count; }

private:
  int find(const uint8_t* key) const;
  struct Entry {
    uint8_t key[KEY_BYTES];
    Meta meta;
  };
  Entry _e[CAPACITY];
  int _count = 0;
};
