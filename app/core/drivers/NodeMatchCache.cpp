#include "NodeMatchCache.h"
#include <string.h>

bool NodeMatchCache::get(const uint8_t* hash, int bytes, bool relays, uint32_t nowMs, NodeMatch* out, int max,
                         int& count) {
  if (bytes < 1 || bytes > 3) return false;
  for (Entry& e : _e) {
    if (!e.bytes || e.bytes != bytes || e.relays != relays || memcmp(e.hash, hash, (size_t)bytes) != 0) continue;
    if (nowMs - e.atMs >= TTL_MS) {
      e.bytes = 0;
      return false;
    }
    if (e.count > e.held && max > e.held) return false;   // the caller wants matches this answer did not keep
    count = e.count;
    for (int i = 0; out && i < e.held && i < max; i++) out[i] = e.m[i];
    return true;
  }
  return false;
}

void NodeMatchCache::put(const uint8_t* hash, int bytes, bool relays, uint32_t nowMs, const NodeMatch* m, int count) {
  if (bytes < 1 || bytes > 3) return;
  Entry* slot = nullptr;
  for (Entry& e : _e) {   // the same question, a free entry, or the oldest answer
    if (e.bytes == bytes && e.relays == relays && memcmp(e.hash, hash, (size_t)bytes) == 0) { slot = &e; break; }
    if (!slot || !e.bytes || (slot->bytes && (int32_t)(e.atMs - slot->atMs) < 0)) slot = &e;
  }
  memcpy(slot->hash, hash, (size_t)bytes);
  slot->bytes = (uint8_t)bytes;
  slot->relays = relays;
  slot->count = (int16_t)count;
  slot->held = (uint8_t)(count < PER ? count : PER);
  for (int i = 0; i < slot->held; i++) slot->m[i] = m[i];
  slot->atMs = nowMs;
}

void NodeMatchCache::clear() {
  for (Entry& e : _e) e.bytes = 0;
}
