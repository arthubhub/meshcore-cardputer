#include "LittleFsStorage.h"
#include <CpadvLog.h>
#include <Arduino.h>
#include <CpadvDataFs.h>   // the data partition (shared with phone mode)
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

// POSIX calls on the mount point, not Arduino's File: each open of a File makes several path lookups (exists, is it a
// directory, stat), and LittleFS spends about 12 ms on each one at 240 MHz. Bench fsops (2026-11-18, 4 KB files):
// a whole read takes 6 ms through open/read/close against 55-59 ms through File. A missing file stays silent: open()
// just fails with ENOENT, without the framework's error line (VALIDATION-6).
namespace {
struct FullPath {
  char p[64];
  explicit FullPath(const char* path) { snprintf(p, sizeof(p), "%s%s", cpadv::DataFs::MOUNT_POINT, path); }
};

bool writeAll(int fd, const void* buf, uint32_t len) {
  const uint8_t* b = (const uint8_t*)buf;
  while (len) {
    const ssize_t n = ::write(fd, b, len);
    if (n <= 0) return false;
    b += n;
    len -= (uint32_t)n;
  }
  return true;
}
}  // namespace

bool LittleFsStorage::read(const char* path, uint32_t off, void* buf, uint32_t len) {
  // A missing file is a normal answer (e.g. MessageStore probes the ring file of a new conversation)
  const FullPath fp(path);
  const int fd = ::open(fp.p, O_RDONLY);
  if (fd < 0) return false;
  bool ok = ::lseek(fd, (off_t)off, SEEK_SET) == (off_t)off;
  uint8_t* b = (uint8_t*)buf;
  uint32_t left = len;
  while (ok && left) {
    const ssize_t n = ::read(fd, b, left);
    if (n <= 0) ok = false;
    else {
      b += n;
      left -= (uint32_t)n;
    }
  }
  ::close(fd);
  return ok;
}

bool LittleFsStorage::write(const char* path, uint32_t off, const void* buf, uint32_t len) {
  // Keeps the content (in-place update of one record) and creates a missing file
  const FullPath fp(path);
  const int fd = ::open(fp.p, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    CPLOG_I("FS", "storage: cannot open %s (errno %d)", path, errno);
    return false;
  }
  bool ok = true;
  const off_t size = ::lseek(fd, 0, SEEK_END);
  if (size < 0) ok = false;
  else if ((off_t)off > size) {   // extend with zeros up to the offset (ring files grow slot by slot)
    static const uint8_t zeros[32] = {};
    uint32_t gap = off - (uint32_t)size;
    while (ok && gap) {
      const uint32_t n = gap > sizeof(zeros) ? sizeof(zeros) : gap;
      ok = writeAll(fd, zeros, n);
      gap -= n;
    }
  }
  ok = ok && ::lseek(fd, (off_t)off, SEEK_SET) == (off_t)off && writeAll(fd, buf, len);
  if (::close(fd) != 0) ok = false;   // LittleFS commits the file at close
  if (!ok) CPLOG_I("FS", "storage: write failed %s @%u (%u bytes, errno %d)", path, (unsigned)off, (unsigned)len, errno);
  return ok;
}

bool LittleFsStorage::writeFile(const char* path, const void* a, uint32_t alen, const void* b, uint32_t blen) {
  // Truncates: LittleFS keeps the old blocks until the close commits the new ones (atomicWriteFile)
  const FullPath fp(path);
  const int fd = ::open(fp.p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    CPLOG_I("FS", "storage: cannot create %s (errno %d)", path, errno);
    return false;
  }
  bool ok = writeAll(fd, a, alen) && (!blen || writeAll(fd, b, blen));
  if (::close(fd) != 0) ok = false;
  if (!ok) CPLOG_I("FS", "storage: write failed %s (%u + %u bytes, errno %d)", path, (unsigned)alen, (unsigned)blen, errno);
  return ok;
}

bool LittleFsStorage::rename(const char* from, const char* to) {
  const FullPath f(from), t(to);
  struct stat st;
  if (::stat(t.p, &st) == 0) return false;   // the contract: `to` must not exist (LittleFS would replace it)
  return ::rename(f.p, t.p) == 0;
}

bool LittleFsStorage::remove(const char* path) {
  const FullPath fp(path);
  return ::unlink(fp.p) == 0;
}

bool LittleFsStorage::makeDir(const char* path) {
  const FullPath fp(path);
  struct stat st;
  if (::stat(fp.p, &st) == 0) return S_ISDIR(st.st_mode);
  if (::mkdir(fp.p, 0755) == 0) return true;
  CPLOG_I("FS", "storage: cannot create directory %s (errno %d)", path, errno);
  return false;
}
