#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
class Stream {
public:
  virtual ~Stream() {}
  virtual size_t write(uint8_t c) { return 1; }
  virtual size_t write(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  virtual void flush() {}
  size_t readBytes(uint8_t*, size_t) { return 0; }
  size_t print(const char*) { return 0; }
  size_t print(char) { return 0; }
  size_t print(int, int = 10) { return 0; }
  size_t println(const char* = "") { return 0; }
  template <class... A> int printf(const char*, A...) { return 0; }
};
