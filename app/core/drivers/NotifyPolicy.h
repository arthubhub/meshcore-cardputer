#pragma once
// What an event deserves (MESSAGING-13 mentions, silent mode): a banner, a sound, waking the screen. One place for the
// rule, used by App (banners and sounds) and by the firmware's loop (waking the screen); portable and unit-tested.
//
// Silent mode (UiSettings::wakeOn), for a Cardputer left on but not watched:
//   WAKE_ALL     every notice wakes the screen and sounds, as before;
//   WAKE_TO_ME   only what is for me (a direct message, a mention of my name on a channel) wakes it and sounds;
//   WAKE_NEVER   nothing wakes it or sounds: messages are stored and counted as unread.
// A banner is always prepared: it shows when the screen is on, whatever the mode.
#include "../Services.h"

enum WakeMode : uint8_t { WAKE_ALL = 0, WAKE_TO_ME = 1, WAKE_NEVER = 2, WAKE_MODES = 3 };

struct Notice {
  bool banner = false;
  bool sound = false;
  SoundKind soundKind = SOUND_CHANNEL_MSG;
  bool wake = false;
  bool toMe = false;            // a direct message or a mention of me
};

class NotifyPolicy {
public:
  static Notice decide(const MeshEvent& ev, uint8_t wakeMode);
  // The sound of an event already on screen (a message in the open conversation): never in WAKE_NEVER, and only for
  // what is for me in WAKE_TO_ME
  static bool soundOnScreen(const MeshEvent& ev, uint8_t wakeMode);
};
