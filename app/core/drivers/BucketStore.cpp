#include "BucketStore.h"
#include "../Log.h"
#include "../hal/Storage.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "STORE";

BucketStore::BucketStore(Storage& storage, const Layout& layout) : _st(storage), _l(layout) {
  _valid = _l.prefix && *_l.prefix && strlen(_l.prefix) <= PREFIX_MAX && _l.recordSize > 0 && _l.buckets > 0 &&
           _l.buckets <= MAX_BUCKETS && _l.maxRecords > 0 && _l.dirs <= MAX_DIRS && _l.dirs <= _l.buckets;
  if (!_valid) {
    CORE_LOGE(TAG, "bucket store '%s': unusable layout (record %u B, %u buckets, %u per bucket)",
              _l.prefix ? _l.prefix : "(null)", (unsigned)_l.recordSize, (unsigned)_l.buckets, (unsigned)_l.maxRecords);
  }
}

const char* BucketStore::statusName(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::Empty: return "empty";
    case Status::Recovered: return "recovered";
    case Status::SetAside: return "set aside";
    case Status::Failed: return "failed";
  }
  return "?";
}

uint32_t BucketStore::crc32(uint32_t crc, const void* data, size_t n) {
  // Half-byte table: 64 bytes of flash, fast enough for one 4 KB bucket (about 8,000 steps)
  static const uint32_t T[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                 0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  const uint8_t* p = (const uint8_t*)data;
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    crc = (crc >> 4) ^ T[crc & 15];
    crc = (crc >> 4) ^ T[crc & 15];
  }
  return ~crc;
}

void BucketStore::name(char* out, size_t n, uint16_t b, const char* ext) const {
  if (_l.dirs) snprintf(out, n, "%s/%x/%02x%s", _l.prefix, (unsigned)(b * _l.dirs / _l.buckets), (unsigned)b, ext);
  else snprintf(out, n, "%s%02x%s", _l.prefix, (unsigned)b, ext);
}

bool BucketStore::ensureDir(uint16_t b) {
  if (!_l.dirs) return true;
  const unsigned d = b * _l.dirs / _l.buckets;
  if (_dirsMade & (1u << d)) return true;
  if (!_prefixMade) {
    if (!_st.makeDir(_l.prefix)) return fail("mkdir", _l.prefix, 0, 0);
    _prefixMade = true;
  }
  char dir[PATH_MAX_LEN];
  snprintf(dir, sizeof(dir), "%s/%x", _l.prefix, d);
  if (!_st.makeDir(dir)) return fail("mkdir", dir, 0, 0);
  _dirsMade |= 1u << d;
  return true;
}

uint32_t BucketStore::headerCrc(const Header& h, const uint8_t* records) const {
  Header copy = h;
  copy.crc = 0;
  uint32_t crc = crc32(0, &copy, sizeof(copy));
  return crc32(crc, records, (size_t)h.count * _l.recordSize);
}

BucketStore::Read BucketStore::readFile(const char* path, uint16_t b, uint8_t* records, uint16_t& count, char* why,
                                        size_t whyLen) {
  count = 0;
  Header h;
  if (!_st.read(path, 0, &h, sizeof(h))) {
    uint8_t one;   // missing, or shorter than a header? Storage only says "not that many bytes"
    if (!_st.read(path, 0, &one, 1)) return Read::Missing;
    snprintf(why, whyLen, "shorter than its header");
    return Read::Invalid;
  }
  if (h.magic != _l.magic) {
    snprintf(why, whyLen, "magic %08lx, expected %08lx", (unsigned long)h.magic, (unsigned long)_l.magic);
    return Read::Invalid;
  }
  if (h.version != _l.version || h.recordSize != _l.recordSize) {
    snprintf(why, whyLen, "format version %u with %u-byte records, this firmware reads version %u with %u",
             (unsigned)h.version, (unsigned)h.recordSize, (unsigned)_l.version, (unsigned)_l.recordSize);
    return Read::Invalid;
  }
  if (h.bucket != b || h.count > _l.maxRecords) {
    snprintf(why, whyLen, "header says bucket %u with %u records (bucket %u, at most %u)", (unsigned)h.bucket,
             (unsigned)h.count, (unsigned)b, (unsigned)_l.maxRecords);
    return Read::Invalid;
  }
  const uint32_t bytes = (uint32_t)h.count * _l.recordSize;
  if (bytes && !_st.read(path, HEADER_BYTES, records, bytes)) {
    snprintf(why, whyLen, "cut short: %u records announced", (unsigned)h.count);
    return Read::Invalid;
  }
  const uint32_t crc = headerCrc(h, records);
  if (crc != h.crc) {
    snprintf(why, whyLen, "CRC %08lx, header says %08lx (%u records)", (unsigned long)crc, (unsigned long)h.crc,
             (unsigned)h.count);
    return Read::Invalid;
  }
  count = h.count;
  return Read::Valid;
}

void BucketStore::setAside(const char* path, uint16_t b, const char* why) {
  char bad[PATH_MAX_LEN];
  name(bad, sizeof(bad), b, ".bad");
  _st.remove(bad);   // keep the latest evidence only: one ".bad" per bucket at most
  const bool kept = _st.rename(path, bad);
  if (!kept) _st.remove(path);
  _stats.setAside++;
  CORE_LOGW(TAG, "%s: %s; %s, bucket %u starts empty", path, why, kept ? "kept as .bad" : "removed (rename failed)",
            (unsigned)b);
}

bool BucketStore::fail(const char* op, const char* path, uint32_t off, uint32_t len) {
  _stats.failures++;
  CORE_LOGE(TAG, "%s %s failed (offset %lu, %lu bytes)", op, path, (unsigned long)off, (unsigned long)len);
  return false;
}

BucketStore::Status BucketStore::load(uint16_t b, uint8_t* records, uint16_t& count) {
  count = 0;
  if (!_valid || b >= _l.buckets || !records) return Status::Failed;
  _stats.loads++;
  char path[PATH_MAX_LEN], fresh[PATH_MAX_LEN], why[96];
  name(path, sizeof(path), b, "");
  name(fresh, sizeof(fresh), b, ".new");

  bool setAsideOld = false;
  switch (readFile(path, b, records, count, why, sizeof(why))) {
    case Read::Valid: return Status::Ok;
    case Read::Invalid:
      setAside(path, b, why);
      setAsideOld = true;
      break;
    case Read::Missing: break;
  }

  if (_st.atomicWriteFile()) {          // saves never leave a ".new" behind: nothing else to look at
    count = 0;
    return setAsideOld ? Status::SetAside : Status::Empty;
  }
  // No usable file: a save may have been cut after it removed the old one
  switch (readFile(fresh, b, records, count, why, sizeof(why))) {
    case Read::Valid:
      if (!_st.rename(fresh, path)) {
        // The content is good: use it now; the next load tries again
        fail("rename", fresh, 0, 0);
      }
      _stats.recovered++;
      CORE_LOGI(TAG, "%s: finished a save cut by a restart (%u records)", path, (unsigned)count);
      return Status::Recovered;
    case Read::Invalid:
      _st.remove(fresh);
      _stats.discarded++;
      CORE_LOGI(TAG, "%s: removed an unfinished save (%s)", fresh, why);
      break;
    case Read::Missing: break;
  }
  count = 0;
  return setAsideOld ? Status::SetAside : Status::Empty;
}

bool BucketStore::save(uint16_t b, const uint8_t* records, uint16_t count) {
  if (!_valid || b >= _l.buckets || count > _l.maxRecords || (count && !records)) {
    _stats.failures++;
    CORE_LOGE(TAG, "save of bucket %u refused (%u records, at most %u, %u buckets)", (unsigned)b, (unsigned)count,
              (unsigned)_l.maxRecords, (unsigned)_l.buckets);
    return false;
  }
  _stats.saves++;
  char path[PATH_MAX_LEN], fresh[PATH_MAX_LEN];
  name(path, sizeof(path), b, "");

  if (count == 0) {
    _st.remove(path);
    if (!_st.atomicWriteFile()) {
      name(fresh, sizeof(fresh), b, ".new");
      _st.remove(fresh);
    }
    return true;
  }
  if (!ensureDir(b)) return false;

  Header h;
  h.magic = _l.magic;
  h.version = _l.version;
  h.recordSize = _l.recordSize;
  h.bucket = b;
  h.count = count;
  h.crc = 0;
  h.crc = headerCrc(h, records);
  const uint32_t bytes = (uint32_t)count * _l.recordSize;

  if (_st.atomicWriteFile()) {           // the file system keeps the old content until the new one is complete
    if (!_st.writeFile(path, &h, sizeof(h), records, bytes)) return fail("write", path, 0, sizeof(h) + bytes);
    return true;
  }
  // The new content beside the old one; a cut in the middle leaves a ".new" that fails its length or CRC check
  name(fresh, sizeof(fresh), b, ".new");
  if (!_st.writeFile(fresh, &h, sizeof(h), records, bytes)) return fail("write", fresh, 0, sizeof(h) + bytes);
  // From here a complete file always exists: the old one, the new one, or both
  _st.remove(path);   // false when the bucket had no file yet
  if (!_st.rename(fresh, path)) return fail("rename", fresh, 0, 0);
  return true;
}
