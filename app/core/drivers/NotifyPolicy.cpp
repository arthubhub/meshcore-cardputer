#include "NotifyPolicy.h"

Notice NotifyPolicy::decide(const MeshEvent& ev, uint8_t wakeMode) {
  Notice n;
  switch (ev.type) {
    case MeshEvent::MESSAGE:
      n.banner = true;
      n.toMe = ev.key.kind == ConvKey::DIRECT;
      n.soundKind = n.toMe ? SOUND_DIRECT_MSG : SOUND_CHANNEL_MSG;
      break;
    case MeshEvent::MENTION:
      n.banner = true;
      n.toMe = true;
      n.soundKind = SOUND_DIRECT_MSG;   // like a direct message, even on a busy channel
      break;
    case MeshEvent::FAILED:
    case MeshEvent::AIRTIME:
    case MeshEvent::CONTACTS_BLOCKED:
      n.banner = true;
      n.soundKind = SOUND_FAIL;
      break;
    case MeshEvent::CONTACT_NEW:
    case MeshEvent::CONTACTS_NEAR_FULL:
    case MeshEvent::CONTACTS_REPLACING:
      n.banner = true;
      n.soundKind = SOUND_CHANNEL_MSG;
      break;
    default:
      return n;                         // DELIVERED and the rest: no notice
  }
  const bool silentKind = ev.type == MeshEvent::CONTACT_NEW || ev.type == MeshEvent::CONTACTS_NEAR_FULL ||
                          ev.type == MeshEvent::CONTACTS_REPLACING;   // these never had a sound
  switch (wakeMode) {
    case WAKE_TO_ME:
      n.wake = n.toMe;
      n.sound = n.toMe;
      break;
    case WAKE_NEVER:
      n.wake = false;
      n.sound = false;
      break;
    default:
      n.wake = true;
      n.sound = !silentKind;
      break;
  }
  return n;
}

bool NotifyPolicy::soundOnScreen(const MeshEvent& ev, uint8_t wakeMode) {
  if (wakeMode == WAKE_NEVER) return false;
  if (wakeMode == WAKE_TO_ME) return decide(ev, wakeMode).toMe;
  return true;
}
