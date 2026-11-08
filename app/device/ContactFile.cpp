#if !CPADV_DEMO   // radio only: the demo firmware has no MeshCore contacts
#include "ContactFile.h"
#include <stdio.h>
#include <string.h>

ContactFile::ContactFile(Storage& storage, const char* path) : _st(storage) {
  snprintf(_path, sizeof(_path), "%s", path);
  snprintf(_temp, sizeof(_temp), "%s.new", path);
}

void ContactFile::toEntry(const RecordV1& r, ContactEntry& e) {
  e = ContactEntry();
  memset(&e.info, 0, sizeof(e.info));
  e.info.id = mesh::Identity(r.pubKey);
  memcpy(e.info.name, r.name, sizeof(e.info.name));
  e.info.name[sizeof(e.info.name) - 1] = 0;
  e.info.type = r.type;
  e.info.flags = r.flags;
  e.info.out_path_len = r.outPathLen;
  memcpy(e.info.out_path, r.outPath, sizeof(e.info.out_path));
  e.info.last_advert_timestamp = r.lastAdvertTs;
  e.info.lastmod = r.lastmod;
  e.info.gps_lat = r.lat;
  e.info.gps_lon = r.lon;
}

bool ContactFile::begin() {
  _st.remove(_temp);            // a temporary file left by an interrupted save
  _inChunk = 0;
  _written = 0;
  _failed = false;
  return true;
}

bool ContactFile::flushChunk() {
  if (!_inChunk || _failed) return !_failed;
  const uint32_t off = sizeof(Header) + _written * sizeof(RecordV2);
  _failed = !_st.write(_temp, off, _chunk, _inChunk * sizeof(RecordV2));
  if (!_failed) _written += _inChunk;
  _inChunk = 0;
  return !_failed;
}

bool ContactFile::add(const ContactEntry& e) {
  RecordV2& r = _chunk[_inChunk];
  memset(&r, 0, sizeof(r));
  memcpy(r.base.pubKey, e.info.id.pub_key, PUB_KEY_SIZE);
  memcpy(r.base.name, e.info.name, sizeof(r.base.name));
  r.base.type = e.info.type;
  r.base.flags = e.info.flags;
  r.base.outPathLen = e.info.out_path_len;
  memcpy(r.base.outPath, e.info.out_path, sizeof(r.base.outPath));
  r.base.lastAdvertTs = e.info.last_advert_timestamp;
  r.base.lastmod = e.info.lastmod;
  r.base.lat = e.info.gps_lat;
  r.base.lon = e.info.gps_lon;
  r.posTime = e.position.time;
  r.posSource = (uint8_t)e.position.source;
  if (++_inChunk == CHUNK) return flushChunk();
  return !_failed;
}

bool ContactFile::commit() {
  if (!flushChunk()) return false;
  Header h{MAGIC_V2, (uint32_t)sizeof(RecordV2), _written};
  if (!_st.write(_temp, 0, &h, sizeof(h))) return false;   // header last: a cut before leaves no valid file
  // From here a complete file always exists: the old one, the temporary one, or the new one
  _st.remove(_path);
  return _st.rename(_temp, _path);
}
#endif  // !CPADV_DEMO
