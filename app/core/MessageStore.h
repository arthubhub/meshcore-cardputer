#pragma once
// Persistent message history, shared by the Cardputer (LittleFS) and the simulator (host files).
//
// - One fixed-size ring file per conversation: the last MAX_MSGS messages, updated in place
//   (an ACK only rewrites one 196-byte record). Bounded flash usage: ~12.5 KB per conversation.
// - A small index (~4 KB of RAM) keeps names, unread counters and the last message preview,
//   so lists and the home screen never touch the flash.
// - Only the messages on screen are loaded (see ChatScreen), never a whole history.
#include <stdint.h>
#include <stddef.h>

// Byte-level file access, implemented by the platform.
class Storage {
public:
  virtual ~Storage() {}
  // Reads exactly `len` bytes at `off`. False if the file is missing or too short.
  virtual bool read(const char* path, uint32_t off, void* buf, uint32_t len) = 0;
  // Writes `len` bytes at `off`, creating / extending the file as needed.
  virtual bool write(const char* path, uint32_t off, const void* buf, uint32_t len) = 0;
  virtual bool remove(const char* path) = 0;
};

constexpr int MSG_TEXT_MAX = 160;     // MeshCore MAX_TEXT_LEN (10 AES blocks)

enum MsgState : uint8_t {
  MSG_RECEIVED = 0,
  MSG_PENDING,       // queued / waiting for the radio
  MSG_SENT_FLOOD,    // on air, flood routed, waiting for the ACK
  MSG_SENT_DIRECT,   // on air, direct path, waiting for the ACK
  MSG_DELIVERED,     // ACK received
  MSG_FAILED,        // no ACK after all retries
  MSG_CHANNEL_SENT,  // channel messages have no ACK
};

struct ConvKey {
  enum Kind : uint8_t { NONE = 0, DIRECT = 1, CHANNEL = 2 };
  uint8_t kind = NONE;
  uint8_t id[6] = {};   // direct: public key prefix; channel: hash of the channel secret

  bool valid() const { return kind != NONE; }
  bool operator==(const ConvKey& o) const;
  bool operator!=(const ConvKey& o) const { return !(*this == o); }
  void path(char* out, size_t n) const;   // flat file name, e.g. "/md0123456789ab"
};

struct StoredMsg {
  uint32_t ts = 0;          // unix time on our clock
  uint32_t ackTag = 0;      // expected ACK (outgoing direct messages)
  uint8_t state = MSG_RECEIVED;
  uint8_t outgoing = 0;
  int8_t snrX4 = 0;         // received: SNR * 4
  uint8_t hops = 0xFF;      // received: number of hops (0xFF unknown)
  uint8_t attempt = 0;      // outgoing: send attempts so far
  char sender[23] = {};     // channel messages: sender name
  char text[MSG_TEXT_MAX + 1] = {};
};

struct ConvMeta {
  ConvKey key;
  uint8_t used = 0;
  uint16_t unread = 0;
  uint32_t lastTs = 0;      // for sorting (our clock)
  uint32_t total = 0;       // messages ever written (= next sequence number)
  char name[32] = {};
  char last[48] = {};       // preview of the last message
  uint8_t lastOutgoing = 0;
};

class MessageStore {
public:
  static constexpr int MAX_CONVS = 40;
  static constexpr int MAX_MSGS = 64;

  void begin(Storage* st);
  // Max conversations kept with a history file (<= MAX_CONVS), from the free flash space.
  void setCapacity(int convs) { _capacity = convs < 4 ? 4 : convs > MAX_CONVS ? MAX_CONVS : convs; }
  int capacity() const { return _capacity; }

  // Creates the conversation if needed (channels are listed even without messages).
  ConvMeta* ensure(const ConvKey& k, const char* name);
  // Appends a message. Returns its sequence number, or -1 on error.
  int32_t append(const ConvKey& k, const char* name, const StoredMsg& m, bool unread);
  bool read(const ConvKey& k, uint32_t seq, StoredMsg& out);
  bool update(const ConvKey& k, uint32_t seq, const StoredMsg& m);
  // Loads up to `max` messages, oldest first, ending `skipNewest` messages before the newest.
  int loadRecent(const ConvKey& k, StoredMsg* out, int max, int skipNewest);
  uint32_t total(const ConvKey& k);   // number of messages available (<= MAX_MSGS)

  void markRead(const ConvKey& k);
  void rename(const ConvKey& k, const char* name);
  void remove(const ConvKey& k);
  int unreadTotal() const;

  int count() const { return _count; }
  const ConvMeta* at(int sortedIndex);   // newest activity first
  const ConvMeta* find(const ConvKey& k) const;

  bool dirty() const { return _dirty; }
  bool flush();   // writes the index if it changed

private:
  ConvMeta* findMut(const ConvKey& k);
  void sort();
  bool writeHeader(const char* path, uint32_t total);

  Storage* _st = nullptr;
  ConvMeta _convs[MAX_CONVS];
  uint8_t _order[MAX_CONVS] = {};
  int _count = 0;
  int _capacity = MAX_CONVS;
  bool _dirty = false;
  bool _sorted = false;
};

// Copies a UTF-8 string, truncating on a code point boundary.
void utf8Copy(char* dst, size_t cap, const char* src);
