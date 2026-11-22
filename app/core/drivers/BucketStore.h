#pragma once
// Fixed-size records kept in small bucket files (MESSAGING-12, docs/storage/NODES.md): the storage layer of the node
// archive. Knows nothing about what a record holds; its owner picks the bucket of a record (the first byte of a key).
//
// Why buckets: LittleFS rewrites the middle of a big file very slowly (the SQLite trials), so a bucket is a file of
// at most one flash block, read and replaced whole.
//
// A file: a 16-byte header (magic, record format version, record size, bucket number, count, CRC-32 of the header and
// the records), then `count` records.
//
// Power cuts: a bucket file is replaced whole, never patched in place.
//   - Storage::atomicWriteFile() (LittleFS): one writeFile(); the file system keeps the old content until the new one
//     is complete. One file operation per save: LittleFS's directory operations cost 10 to 300 ms each on the
//     Cardputer (bench fsops, 2026-11-18), so every one avoided counts.
//   - otherwise (FAT, the pessimistic PC fake): the new content goes to "<name>.new", the old file is removed, the new
//     one renamed. load() then finds the old content or the new one:
//       - old file present: it wins; a ".new" beside it is an unfinished save, replaced by the next save;
//       - old file missing, ".new" valid: the cut came between the remove and the rename; load() finishes the save;
//       - ".new" invalid: the save was cut while writing; it is removed.
// A damaged file (bad CRC, wrong bucket, too many records) or a file of another format version is moved to "<name>.bad"
// (the previous one is replaced), logged with what was found, and the bucket starts empty: the other buckets are
// untouched, and the evidence stays for a serial capture.
//
// Directories: LittleFS's directory operations get slower with every entry of the directory (a lookup takes 9 ms
// with 16 files, 16 ms with 160). Layout::dirs spreads the buckets over that many subdirectories.
#include <stddef.h>
#include <stdint.h>

class Storage;

class BucketStore {
public:
  static constexpr int MAX_BUCKETS = 256;
  static constexpr uint32_t HEADER_BYTES = 16;
  static constexpr int PREFIX_MAX = 16;
  static constexpr int PATH_MAX_LEN = PREFIX_MAX + 16;
  static constexpr int MAX_DIRS = 32;

  struct Layout {
    // dirs == 0: file names are prefix + 2 hex digits ("/nb" -> "/nb3f", "/nb3f.new", "/nb3f.bad");
    // dirs > 0: prefix is a directory, bucket b goes to its subdirectory b * dirs / buckets ("/nodes" -> "/nodes/3/3f")
    const char* prefix = "";
    uint8_t dirs = 0;
    uint32_t magic = 0;           // what the records are
    uint16_t version = 1;         // of the record format; another version is set aside, not read
    uint16_t recordSize = 0;
    uint16_t buckets = 0;         // 1 .. MAX_BUCKETS
    uint16_t maxRecords = 0;      // per file, so that a file stays within one flash block
  };

  enum class Status : uint8_t {
    Ok,          // read
    Empty,       // no file: a bucket never written, or emptied
    Recovered,   // a save cut between its last two steps was finished
    SetAside,    // damaged or of another version: moved to ".bad", the bucket starts empty
    Failed,      // the storage refused an operation (the content on flash is unchanged)
  };

  struct Stats {
    uint32_t loads = 0, saves = 0;
    uint32_t recovered = 0;       // saves finished by load()
    uint32_t discarded = 0;       // unfinished ".new" files removed
    uint32_t setAside = 0;
    uint32_t failures = 0;
  };

  BucketStore(Storage& storage, const Layout& layout);

  // False when the layout cannot work (no prefix, size 0, too many buckets): every call then fails.
  bool valid() const { return _valid; }
  const Layout& layout() const { return _l; }
  // Bytes of the buffer that load() and save() take: maxRecords records
  uint32_t bufferBytes() const { return (uint32_t)_l.recordSize * _l.maxRecords; }
  uint32_t fileBytes(uint16_t count) const { return HEADER_BYTES + (uint32_t)_l.recordSize * count; }

  // Reads bucket `b` into `records` (bufferBytes() long); `count` is how many records it holds (0 unless Ok/Recovered).
  Status load(uint16_t b, uint8_t* records, uint16_t& count);
  // Replaces bucket `b` with `count` records; 0 removes its file. False when the storage failed: load() then still
  // gives the previous content or, if the cut came after the old file was removed, the new one.
  bool save(uint16_t b, const uint8_t* records, uint16_t count);

  const Stats& stats() const { return _stats; }
  static const char* statusName(Status s);
  // CRC-32 (IEEE 802.3, the zlib one), `crc` = 0 to start
  static uint32_t crc32(uint32_t crc, const void* data, size_t n);

private:
  enum class Read : uint8_t { Valid, Missing, Invalid };
  struct Header {
    uint32_t magic;
    uint16_t version, recordSize, bucket, count;
    uint32_t crc;
  };
  static_assert(sizeof(Header) == HEADER_BYTES, "header layout");

  void name(char* out, size_t n, uint16_t b, const char* ext) const;
  bool ensureDir(uint16_t b);
  Read readFile(const char* path, uint16_t b, uint8_t* records, uint16_t& count, char* why, size_t whyLen);
  uint32_t headerCrc(const Header& h, const uint8_t* records) const;
  void setAside(const char* path, uint16_t b, const char* why);
  bool fail(const char* op, const char* path, uint32_t off, uint32_t len);

  Storage& _st;
  Layout _l;
  bool _valid = false;
  uint32_t _dirsMade = 0;               // bit per subdirectory known to exist (and bit 31 + dirs: the prefix itself)
  bool _prefixMade = false;
  Stats _stats;
};
