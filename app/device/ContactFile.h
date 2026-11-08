#pragma once
// The contacts file of the keyboard firmware (/contacts.cp). Depends on MeshCore and the Storage interface only, so
// app/sim/wire tests it on real ContactInfo records.
//  - version 2 adds the time and source of each position (MESSAGING-11); version 1 files still load;
//  - saving never leaves the device without contacts: the new list goes to a temporary file, which then replaces the
//    old one. A power cut at any moment leaves one complete file, which load() finds.
#include <helpers/ContactInfo.h>
#include "../core/MessageStore.h"
#include "../core/drivers/PositionBook.h"

struct ContactEntry {
  ContactInfo info;
  PositionBook::Meta position;
};

class ContactFile {
public:
  static constexpr uint32_t MAGIC_V1 = 0x31434350;   // "PCC1": records until the end of the file
  static constexpr uint32_t MAGIC_V2 = 0x32434350;   // "PCC2": header with the count

  ContactFile(Storage& storage, const char* path);

  // Calls `take` for each contact; it returns false to stop (no room left). Returns how many were read.
  template <class Take> int load(Take take) {
    int n = loadFrom(_path, take);
    return n >= 0 ? n : loadFrom(_temp, take) >= 0 ? _lastCount : 0;
  }

  // Writes the whole list: begin(), add() for each contact, commit().
  bool begin();
  bool add(const ContactEntry& e);
  bool commit();

  int lastFormat() const { return _lastFormat; }   // 1 or 2: the version found by the last load

private:
  struct Header { uint32_t magic, recordSize, count; };
  struct RecordV1 {            // the layout of version 1, unchanged
    uint8_t pubKey[PUB_KEY_SIZE];
    char name[32];
    uint8_t type, flags, outPathLen, reserved;
    uint8_t outPath[MAX_PATH_SIZE];
    uint32_t lastAdvertTs, lastmod;
    int32_t lat, lon;
  };
  struct RecordV2 {
    RecordV1 base;
    uint32_t posTime;
    uint8_t posSource;
    uint8_t reserved[3];
  };
  static constexpr int CHUNK = 4;

  static void toEntry(const RecordV1& r, ContactEntry& e);
  bool flushChunk();

  template <class Take> int loadFrom(const char* path, Take take) {
    Header h;
    if (!_st.read(path, 0, &h, sizeof(h))) return -1;
    int n = 0;
    ContactEntry e;
    if (h.magic == MAGIC_V2 && h.recordSize == sizeof(RecordV2)) {
      RecordV2 r;
      for (uint32_t i = 0; i < h.count && _st.read(path, sizeof(h) + i * sizeof(r), &r, sizeof(r)); i++) {
        toEntry(r.base, e);
        e.position.time = r.posTime;
        e.position.source = (PositionSource)(r.posSource <= 2 ? r.posSource : 0);
        n++;
        if (!take(e)) break;
      }
      _lastFormat = 2;
    } else if (h.magic == MAGIC_V1) {
      RecordV1 r;
      for (uint32_t off = 4; _st.read(path, off, &r, sizeof(r)); off += sizeof(r)) {
        toEntry(r, e);
        if (e.info.gps_lat || e.info.gps_lon) {   // the position came from the last advert
          e.position.time = e.info.last_advert_timestamp;
          e.position.source = PositionSource::Advert;
        }
        n++;
        if (!take(e)) break;
      }
      _lastFormat = 1;
    } else {
      return -1;
    }
    _lastCount = n;
    return n;
  }

  Storage& _st;
  char _path[24], _temp[28];
  RecordV2 _chunk[CHUNK];
  int _inChunk = 0;
  uint32_t _written = 0;
  bool _failed = false;
  int _lastFormat = 0;
  int _lastCount = 0;
};
