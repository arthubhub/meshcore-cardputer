#pragma once
// One node of the archive (MESSAGING-12, docs/storage/NODES.md): every node heard, whether or not it is in MeshCore's
// contact table. NodeRecord is what the code uses; NodeRecordCodec is the only code that knows its bytes on flash.
//
// Record version 1, 96 bytes, little endian, field by field (never a memcpy of the struct):
//    0 pubKey[32]   32 name[32] (UTF-8, NUL padded)   64 type   65 flags   66 hops (0xFF unknown)   67 snrX4 (int8)
//   68 rssi (int8, dBm)   69 posSource   70 reserved[2]   72 latE6   76 lonE6   80 posTime   84 firstHeard
//   88 lastHeard   92 lastAdvertTs
// No CRC per record: the bucket's CRC covers it (BucketStore).
#include <stddef.h>
#include <stdint.h>

struct NodeRecord {
  enum Flag : uint8_t {
    Favourite = 0x01,
    InContacts = 0x02,     // in MeshCore's contact table right now
    Keep = 0x04,           // has a conversation, or was a contact: must stay findable
    Imported = 0x08,       // from a file, never heard (SYNC-9)
    HasPos = 0x10,
  };
  static constexpr uint8_t PROTECTED = Favourite | InContacts | Keep;   // never dropped from a full bucket
  static constexpr uint8_t HOPS_UNKNOWN = 0xFF;

  uint8_t pubKey[32] = {};
  char name[32] = {};
  uint8_t type = 0;              // ContactType (Services.h): 1 person, 2 repeater, 3 room, 4 sensor
  uint8_t flags = 0;
  uint8_t hops = HOPS_UNKNOWN;   // of the last hearing (an advert's path)
  int8_t snrX4 = 0;              // of the last hop of the last hearing
  int8_t rssi = 0;               // dBm, idem
  uint8_t posSource = 0;         // PositionSource (ContactPosition.h)
  int32_t latE6 = 0, lonE6 = 0;
  uint32_t posTime = 0;          // when it was there, by its clock (unix), 0 unknown
  uint32_t firstHeard = 0;       // our clock (unix), 0 unknown
  uint32_t lastHeard = 0;
  uint32_t lastAdvertTs = 0;     // its clock: MeshCore's replay check when it comes back into the contact table

  bool has(Flag f) const { return (flags & f) != 0; }
  bool isProtected() const { return (flags & PROTECTED) != 0; }
  bool hasPos() const { return has(HasPos); }
  static int8_t clampRssi(int dbm) { return (int8_t)(dbm < -128 ? -128 : dbm > 127 ? 127 : dbm); }
};

class NodeRecordCodec {
public:
  static constexpr int BYTES = 96;
  static constexpr uint16_t VERSION = 1;

  static void encode(const NodeRecord& r, uint8_t* out);   // BYTES bytes
  static void decode(const uint8_t* in, NodeRecord& r);    // the name always ends with a NUL
};
