#pragma once
// Minimal dependency-free PNG writer (RGB8, zlib "stored" blocks = no compression).
// Files are larger than optimal but this keeps the simulator free of zlib/libpng.
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <string>

namespace png {

inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c;
}

inline void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

inline void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  put32(out, (uint32_t)data.size());
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put32(out, crc32(&out[start], out.size() - start) ^ 0xFFFFFFFFu);
}

// rgb: width*height*3 bytes, row-major.
inline bool write(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb) {
  std::vector<uint8_t> raw;  // filter byte 0 + row data
  raw.reserve((size_t)(width * 3 + 1) * height);
  for (int y = 0; y < height; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb.begin() + (size_t)y * width * 3, rgb.begin() + (size_t)(y + 1) * width * 3);
  }

  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  for (size_t off = 0; off < raw.size() || off == 0;) {
    size_t len = raw.size() - off;
    if (len > 65535) len = 65535;
    bool last = off + len >= raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(len & 0xFF); z.push_back(len >> 8);
    z.push_back(~len & 0xFF); z.push_back((~len >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + len);
    off += len;
    if (last) break;
  }
  put32(z, (b << 16) | a);

  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, width); put32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit, RGB, deflate, no filter, no interlace
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});

  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  return ok;
}

}  // namespace png
