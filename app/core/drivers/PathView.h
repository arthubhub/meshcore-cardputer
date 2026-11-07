#pragma once
// Reads a MeshCore path: the list of repeater IDs a packet went through (MESSAGING-10). One wire byte says how: bits 6-7
// are the size of each ID minus 1, bits 0-5 the number of hops. Every place that holds a path reads it through here.
#include <stddef.h>
#include <stdint.h>

class PathView {
public:
  // `bytes` holds at most `cap` bytes of the path: a longer one is cut, and truncated() says so.
  PathView(uint8_t wire, const uint8_t* bytes, size_t cap) : _wire(wire), _bytes(bytes), _cap(bytes ? cap : 0) {}

  int hops() const { return _wire & 63; }                // as sent, even if not all of them are held
  int hashBytes() const { return (_wire >> 6) + 1; }
  bool validSize() const { return hashBytes() <= 3; }    // size 4 is reserved by MeshCore
  int heldHops() const;
  bool truncated() const { return heldHops() < hops(); }
  const uint8_t* hash(int i) const { return _bytes + i * hashBytes(); }   // i < heldHops()

  // Hex route "a1>b2>c3". Ends with ".." when part of it is not held or does not fit in `out`.
  void format(char* out, size_t n) const;

private:
  uint8_t _wire;
  const uint8_t* _bytes;
  size_t _cap;
};

// A copy of a path that fits in a message record.
enum MsgPathKind : uint8_t {
  PATH_NONE = 0,    // no route known
  PATH_TAKEN,       // read from the packet itself: the route it really took
  PATH_PLANNED,     // the route stored for this contact: where it should have gone, not a proof
};

constexpr int MSG_PATH_MAX = 32;   // 32 hops with 1 byte IDs, 16 with 2, 10 with 3

struct MsgRoute {
  uint8_t kind = PATH_NONE;
  uint8_t wire = 0;
  uint8_t bytes[MSG_PATH_MAX] = {};

  void set(MsgPathKind k, uint8_t pathWire, const uint8_t* path);
  // The same route travelled the other way: entries in reverse order, each entry unchanged.
  void setReversed(MsgPathKind k, uint8_t pathWire, const uint8_t* path);
  PathView view() const { return PathView(wire, bytes, sizeof(bytes)); }
  bool known() const { return kind != PATH_NONE; }
};
