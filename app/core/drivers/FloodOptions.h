#pragma once
// How this node floods packets: the two user settings behind every flood (RADIO-9, RADIO-10). Portable checks only;
// device/FloodPolicy applies them through MeshCore.
//
// - Path ID size: how many bytes of a repeater's public key are written in a packet's path. MeshCore allows 1, 2 or 3.
//   Repeaters older than firmware 1.14 silently drop 2 and 3 byte packets (meshcore/docs/faq.md, 3.9.1).
// - Region (MeshCore's "flood scope"): a name whose key gives each flood a transport code. A repeater forwards the
//   flood only if it knows that region, so a name the local repeaters do not use stops the flood at the first hop.
#include <stddef.h>
#include <stdint.h>

namespace floodopt {

constexpr uint8_t PATH_BYTES_MIN = 1, PATH_BYTES_MAX = 3;
constexpr size_t REGION_MAX = 30;        // RegionEntry::name and NodePrefs::default_scope_name hold 31 bytes

// A saved value out of range (damaged file) falls back to the safe default.
inline uint8_t validPathBytes(uint8_t n) { return n >= PATH_BYTES_MIN && n <= PATH_BYTES_MAX ? n : PATH_BYTES_MIN; }
// Next value of the setting, wrapping around.
uint8_t stepPathBytes(uint8_t n, int delta);

enum class RegionName : uint8_t {
  None,      // empty: floods stay unscoped
  Ok,
  Private,   // "$name": its key is not derived from the name, it cannot be typed here
  Invalid,   // too long, or a character RegionMap::is_name_char refuses
};

// Cleans what the user typed: trims spaces, removes a leading '#' (repeaters ignore it too). The case is kept: the key
// comes from the exact bytes, so "FR" and "fr" are two regions. `out` receives "" unless the result is Ok.
RegionName normalizeRegion(const char* in, char* out, size_t cap);

}  // namespace floodopt
