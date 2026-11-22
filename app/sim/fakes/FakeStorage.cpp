#include "FakeStorage.h"
#include <string.h>

void FakeStorage::cutAt(uint32_t op, uint32_t keepBytes) {
  _armed = true;
  _cutIn = op;
  _keep = keepBytes;
}

void FakeStorage::reboot() {
  _off = false;
  _armed = false;
  _cutting = false;
}

bool FakeStorage::change() {
  _cutting = false;
  if (_off) return false;
  _changes++;
  if (_armed && _cutIn-- == 0) {
    _armed = false;
    _off = true;
    _cutting = true;
    return false;
  }
  return true;
}

bool FakeStorage::read(const char* path, uint32_t off, void* buf, uint32_t len) {
  if (_off) return false;
  reads++;
  auto it = files.find(path);
  if (it == files.end() || (uint64_t)off + len > it->second.size()) return false;
  memcpy(buf, it->second.data() + off, len);
  return true;
}

bool FakeStorage::parentExists(const std::string& path) const {
  if (!_requireDirs) return true;
  const size_t slash = path.rfind('/');
  if (slash == std::string::npos || slash == 0) return true;   // the root
  return dirs.count(path.substr(0, slash)) > 0;
}

bool FakeStorage::makeDir(const char* path) {
  if (!change()) return false;
  if (!parentExists(path)) return false;
  dirs.insert(path);
  return true;
}

bool FakeStorage::writeFile(const char* path, const void* a, uint32_t alen, const void* b, uint32_t blen) {
  if (!_atomic) return Storage::writeFile(path, a, alen, b, blen);
  if (_failWrites && !_off) return false;
  if (!change()) return false;         // cut: LittleFS keeps the previous file whole
  if (!parentExists(path)) return false;
  writes++;
  std::vector<uint8_t> f(alen + blen);
  if (alen) memcpy(f.data(), a, alen);
  if (blen) memcpy(f.data() + alen, b, blen);
  files[path] = f;
  return true;
}

bool FakeStorage::write(const char* path, uint32_t off, const void* buf, uint32_t len) {
  if (_failWrites && !_off) return false;
  if (!files.count(path) && !parentExists(path)) return false;
  const bool whole = change();
  if (!whole && !_cutting) return false;
  writes++;
  const uint32_t n = whole ? len : (_keep < len ? _keep : len);
  if (!whole && n == 0) return false;   // cut before a byte reached the flash
  std::vector<uint8_t>& f = files[path];
  if (f.size() < (size_t)off + n) f.resize((size_t)off + n);   // a gap before `off` reads as zeros
  if (n) memcpy(f.data() + off, buf, n);
  return whole;
}

bool FakeStorage::remove(const char* path) {
  if (!change()) return false;
  removes++;
  return files.erase(path) > 0;
}

bool FakeStorage::rename(const char* from, const char* to) {
  if (!change()) return false;
  renames++;
  auto it = files.find(from);
  if (it == files.end() || files.count(to)) return false;
  files[to] = it->second;
  files.erase(from);
  return true;
}
