#pragma once
// Keeps each contact's newest position, whatever carries it (MESSAGING-11). MeshCore writes an advert's position over
// the contact's; this class puts back a newer one received in a message. Depends on MeshCore only, so app/sim/wire
// runs it with BaseChatMesh's real advert handling.
#include <helpers/ContactInfo.h>
#include "../core/drivers/PositionBook.h"

class ContactPositions {
public:
  PositionBook& book() { return _book; }
  PositionBook::Meta meta(const ContactInfo& c) const;

  // Takes a position if it is not older than the contact's. True if it changed.
  bool offer(ContactInfo& c, int32_t latE6, int32_t lonE6, uint32_t when, PositionSource source);
  // A position written in a direct message, which is authenticated. `found` says whether the text held one.
  bool fromMessage(ContactInfo& c, const char* text, uint32_t senderTime, bool& found);

  // Around BaseChatMesh::onAdvertRecv: what the contact held before, then the advert's position offered with its time.
  struct Before {
    bool known = false;
    int32_t latE6 = 0, lonE6 = 0;
  };
  static Before before(const ContactInfo* existing);
  // True if the contact's position changed. `c` is the contact after MeshCore handled the advert (nullptr: not added).
  bool afterAdvert(const Before& before, ContactInfo* c, uint32_t timestamp, const uint8_t* appData, size_t appDataLen);

private:
  PositionBook _book;
};
