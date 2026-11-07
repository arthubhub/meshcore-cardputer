#pragma once
// Files are not part of what sim/wire tests: region and identity storage stay unused.
#include "Stream.h"
#define FILE_READ "r"
class File : public Stream {
public:
  explicit operator bool() const { return false; }
  size_t read(uint8_t*, size_t) { return 0; }
  size_t write(const uint8_t*, size_t n) override { return n; }
  using Stream::read;
  size_t size() { return 0; }
  void close() {}
};
namespace fs {
class FS {
public:
  File open(const char*, const char* = "r", bool = false) { return File(); }
  bool exists(const char*) { return false; }
  bool mkdir(const char*) { return false; }
  bool remove(const char*) { return false; }
};
}  // namespace fs
