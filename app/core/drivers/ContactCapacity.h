#pragma once
// How full the contact list is, and when to say so (VALIDATION-11). A full list used to refuse every new contact without
// a word, so their messages could not be decrypted. Now the oldest non-favourite is replaced, and the user is told once
// when the list nears its limit and once when replacing starts, not at every advert.
#include <stdint.h>

class ContactCapacity {
public:
  static constexpr int WARN_PERCENT = 90;
  static constexpr int REARM_PERCENT = 85;    // a warning comes back only after the list went down below this

  enum class Alert : uint8_t { None, Near, Full };

  // Reports each rise of the level once.
  Alert poll(int count, int max);
  static bool near(int count, int max) { return max > 0 && count * 100 >= max * WARN_PERCENT; }

private:
  Alert _level = Alert::None;
};
