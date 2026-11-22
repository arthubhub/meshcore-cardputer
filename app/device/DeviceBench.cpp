#include "DeviceBench.h"
#include "LittleFsStorage.h"
#include "../core/drivers/StoreBench.h"
#include <Arduino.h>
#include <target.h>
#include <CpadvDataFs.h>
#include <CpadvLog.h>
#include <CpadvRadioMonitor.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if !CPADV_DEMO
#include <Identity.h>
#endif


namespace {
LittleFsStorage g_storage;

class Host : public StoreBenchHost {
public:
  uint32_t micros() override { return ::micros(); }
  int64_t fsUsedBytes() override { return (int64_t)cpadv::dataFs().usedBytes(); }
  void log(bool error, const char* line) override {
    if (error) CPLOG_E("BENCH", "%s", line);
    else CPLOG_I("BENCH", "%s", line);
  }
};
Host g_host;

// "key=value" in a space-separated list; false when absent
bool option(const char* args, const char* key, char* out, size_t n) {
  const size_t k = strlen(key);
  for (const char* p = args; p && *p;) {
    while (*p == ' ') p++;
    if (!strncmp(p, key, k) && p[k] == '=') {
      const char* v = p + k + 1;
      size_t len = strcspn(v, " ");
      if (len >= n) len = n - 1;
      memcpy(out, v, len);
      out[len] = 0;
      return true;
    }
    p = strchr(p, ' ');
  }
  return false;
}
int intOption(const char* args, const char* key, int dflt) {
  char v[12];
  return option(args, key, v, sizeof(v)) ? atoi(v) : dflt;
}
}  // namespace

void DeviceBench::command(const char* args) {
  if (!strncmp(args, "store", 5)) startStore(args + 5);
  else if (!strncmp(args, "ecdh", 4)) ecdh(args + 4);
  else if (!strncmp(args, "clean", 5)) clean();
  else if (!strncmp(args, "fsops", 5)) startFsOps(args + 5);
  else CPLOG_I("BENCH", "usage: bench store [dir=root|sub|fan] [buckets=128] [task=0|1] [mhz=80|240] | bench fsops [files=160] [size=4048] [dir=root|sub] | bench ecdh [n=16] | bench clean");
}

void DeviceBench::startStore(const char* args) {
  if (_bench) {
    CPLOG_W("BENCH", "a bench is already running (%s)", StoreBench::phaseName(_bench->phase()));
    return;
  }
  char dir[8] = "root";
  option(args, "dir", dir, sizeof(dir));
  uint8_t dirs = 0;
  if (!strcmp(dir, "fan")) {                       // the archive's own layout: 8 subdirectories
    snprintf(_prefix, sizeof(_prefix), "/nbench");
    dirs = NodeArchive::DIRS;
  } else if (!strcmp(dir, "sub")) {
    if (!cpadv::dataFs().exists("/nbench") && !cpadv::dataFs().mkdir("/nbench")) {
      CPLOG_E("BENCH", "cannot create /nbench on the data partition");
      return;
    }
    snprintf(_prefix, sizeof(_prefix), "/nbench/nb");
  } else {
    snprintf(_prefix, sizeof(_prefix), "/nbt");
  }
  _inTask = intOption(args, "task", 0) != 0;
  const int mhz = intOption(args, "mhz", 0);
  _cpuBefore = getCpuFrequencyMhz();
  if (mhz == 80 || mhz == 160 || mhz == 240) setCpuFrequencyMhz(mhz);

  StoreBench::Options o;
  o.prefix = _prefix;
  o.dirs = dirs;
  o.buckets = intOption(args, "buckets", NodeArchive::BUCKETS);
  o.lookups = intOption(args, "lookups", 128);
  _bench = new StoreBench(g_storage, g_host, o);   // about 14 KB, freed at the end
  if (!_bench) {
    CPLOG_E("BENCH", "no memory for the bench (heap %u)", ESP.getFreeHeap());
    return;
  }
  if (cpadv::RadioMonitor* m = board.radioMonitor()) {
    _rxBefore = m->received();
    _errBefore = m->errors();
  }
  _lastLoopMs = millis();
  _maxLoopGapMs = 0;
  _taskDone = false;
  CPLOG_I("BENCH", "store bench: files %s*, CPU %u MHz, %s, heap %u", _prefix, (unsigned)getCpuFrequencyMhz(),
          _inTask ? "in its own task (loop() keeps running)" : "one operation per pass of loop()", ESP.getFreeHeap());
  if (_inTask && xTaskCreatePinnedToCore(task, "bench", 6144, this, 1, nullptr, 1) != pdPASS) {
    CPLOG_E("BENCH", "cannot start the bench task");
    _inTask = false;
  }
}

void DeviceBench::task(void* self) {
  DeviceBench* b = (DeviceBench*)self;
  while (b->_bench->step()) vTaskDelay(1);
  b->_taskDone = true;
  vTaskDelete(nullptr);
}

void DeviceBench::loop() {
  if (_fs.running) {
    stepFsOps();
    return;
  }
  if (!_bench) return;
  // From the start of one pass to the start of the next: includes the bench's own step (task=0) and the rest of loop()
  const uint32_t now = millis();
  if (now - _lastLoopMs > _maxLoopGapMs) _maxLoopGapMs = now - _lastLoopMs;
  _lastLoopMs = now;
  if (_inTask) {
    if (_taskDone) finishStore();
  } else if (!_bench->step()) {
    finishStore();
  }
}

void DeviceBench::finishStore() {
  _bench->report();
  CPLOG_I("BENCH", "  longest pause of loop() during the bench: %lu ms (%s)", (unsigned long)_maxLoopGapMs,
          _inTask ? "storage in a task" : "storage in loop()");
  if (cpadv::RadioMonitor* m = board.radioMonitor()) {
    CPLOG_I("BENCH", "  radio during the bench: %lu packets received, %lu receive errors",
            (unsigned long)(m->received() - _rxBefore), (unsigned long)(m->errors() - _errBefore));
  }
  delete _bench;
  _bench = nullptr;
  if (getCpuFrequencyMhz() != _cpuBefore) setCpuFrequencyMhz(_cpuBefore);
}

void DeviceBench::clean() {
  if (running()) {
    CPLOG_W("BENCH", "a bench is running");
    return;
  }
  int removed = 0;
  char path[32];
  static const char* const exts[] = {"", ".new", ".bad"};
  for (int b = 0; b < 256; b++) {
    for (const char* e : exts) {
      const char* candidates[3];
      char p0[32], p1[32], p2[32];
      snprintf(p0, sizeof(p0), "/nbt%02x%s", b, e);
      snprintf(p1, sizeof(p1), "/nbench/nb%02x%s", b, e);
      snprintf(p2, sizeof(p2), "/nbench/%x/%02x%s", b * NodeArchive::DIRS / NodeArchive::BUCKETS, b, e);
      candidates[0] = p0;
      candidates[1] = p1;
      candidates[2] = p2;
      for (const char* c : candidates) {
        snprintf(path, sizeof(path), "%s", c);
        if (cpadv::dataFs().exists(path) && cpadv::dataFs().remove(path)) removed++;
      }
    }
  }
  for (int d = 0; d < NodeArchive::DIRS; d++) {
    snprintf(path, sizeof(path), "/nbench/%x", d);
    if (cpadv::dataFs().exists(path)) cpadv::dataFs().rmdir(path);
  }
  if (cpadv::dataFs().exists("/nbench")) cpadv::dataFs().rmdir("/nbench");
  CPLOG_I("BENCH", "removed %d bench files, data partition %u / %u KB used", removed,
          (unsigned)(cpadv::dataFs().usedBytes() / 1024), (unsigned)(cpadv::dataFs().totalBytes() / 1024));
}

void DeviceBench::ecdh(const char* args) {
#if CPADV_DEMO
  (void)args;
  CPLOG_I("BENCH", "no MeshCore identity in the demo firmware");
#else
  if (!_rng) return;
  int n = intOption(args, "n", 16);
  if (n < 1) n = 1;
  if (n > 64) n = 64;
  const uint32_t before = getCpuFrequencyMhz();
  mesh::LocalIdentity self(_rng);
  static const uint32_t speeds[] = {240, 80};
  for (uint32_t mhz : speeds) {
    setCpuFrequencyMhz(mhz);
    TimingStats t;
    uint8_t secret[PUB_KEY_SIZE];
    for (int i = 0; i < n; i++) {
      mesh::LocalIdentity other(_rng);   // a real key: the cost of a candidate
      const uint32_t s = micros();
      self.calcSharedSecret(secret, other.pub_key);
      t.add(micros() - s);
      delay(1);                          // let loop()'s other task and the watchdog breathe
    }
    char line[96];
    t.format(line, sizeof(line));
    CPLOG_I("BENCH", "ECDH at %lu MHz: %s", (unsigned long)mhz, line);
  }
  setCpuFrequencyMhz(before);
#endif
}

// ---------------------------------------------------------------- bench fsops

void DeviceBench::startFsOps(const char* args) {
  if (_fs.running || _bench) {
    CPLOG_W("BENCH", "a bench is already running");
    return;
  }
  _fs = FsOps();
  _fs.files = intOption(args, "files", 160);
  _fs.size = intOption(args, "size", 4048);
  char dir[8] = "root";
  option(args, "dir", dir, sizeof(dir));
  if (!strcmp(dir, "sub")) {
    if (!cpadv::dataFs().exists("/nftd") && !cpadv::dataFs().mkdir("/nftd")) {
      CPLOG_E("BENCH", "cannot create /nftd");
      return;
    }
    snprintf(_fs.prefix, sizeof(_fs.prefix), "/nftd/f");
  }
  _fs.data = (uint8_t*)malloc(8192);
  if (!_fs.data) {
    CPLOG_E("BENCH", "no memory for fsops (heap %u)", ESP.getFreeHeap());
    return;
  }
  _fs.running = true;
  CPLOG_I("BENCH", "fsops: %d files of %d B as %s###, CPU %u MHz, data partition %u KB used", _fs.files, _fs.size,
          _fs.prefix, (unsigned)getCpuFrequencyMhz(), (unsigned)(cpadv::dataFs().usedBytes() / 1024));
  CPLOG_I("BENCH", "  files | create+write+close | stat | rewrite 16 B at 0 | rename | rename back | (avg / max ms)");
}

void DeviceBench::stepFsOps() {
  uint8_t* data = _fs.data;
  uint8_t* back = _fs.data + 4096;
  fs::FS& fs = cpadv::dataFs();
  char path[24], other[28];
  snprintf(path, sizeof(path), "%s%03x", _fs.prefix, _fs.i);
  snprintf(other, sizeof(other), "%s%03x.n", _fs.prefix, _fs.i);
  uint32_t t[FsOps::OPS] = {};
  if (!_fs.removing) {
    for (size_t k = 0; k < 4096; k++) data[k] = (uint8_t)(k * 31 + _fs.i);
    uint32_t s = micros();
    File f = fs.open(path, "w");
    bool ok = f && f.write(data, _fs.size > 4096 ? 4096 : _fs.size) == (size_t)(_fs.size > 4096 ? 4096 : _fs.size);
    if (f) f.close();
    t[0] = micros() - s;
    s = micros();
    ok = ok && cpadv::dataFs().exists(path);
    t[1] = micros() - s;
    s = micros();
    f = fs.open(path, "r+");
    ok = ok && f && f.seek(0) && f.write(data, 16) == 16;
    if (f) f.close();
    t[2] = micros() - s;
    s = micros();
    ok = ok && fs.rename(path, other);
    t[3] = micros() - s;
    s = micros();
    ok = ok && fs.rename(other, path);
    t[4] = micros() - s;
    const size_t sz = _fs.size > 4096 ? 4096 : _fs.size;
    s = micros();
    f = cpadv::dataFs().openIfExists(path);
    ok = ok && f && f.read(back, sz) == sz;
    if (f) f.close();
    t[6] = micros() - s;
    s = micros();
    f = cpadv::dataFs().openIfExists(path);
    ok = ok && f && f.read(back, 16) == 16;
    if (f) f.close();
    f = cpadv::dataFs().openIfExists(path);
    ok = ok && f && f.seek(16) && f.read(back + 16, sz - 16) == sz - 16;
    if (f) f.close();
    t[7] = micros() - s;
    // The same through POSIX calls: one LittleFS lookup per open, no Arduino File layer
    char full[48];
    snprintf(full, sizeof(full), "%s%s", cpadv::DataFs::MOUNT_POINT, path);
    s = micros();
    int fd = ::open(full, O_RDONLY);
    ok = ok && fd >= 0 && ::read(fd, back, sz) == (ssize_t)sz;
    if (fd >= 0) ::close(fd);
    t[8] = micros() - s;
    s = micros();
    fd = ::open(full, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ok = ok && fd >= 0 && ::write(fd, data, sz) == (ssize_t)sz;
    if (fd >= 0) ::close(fd);
    t[9] = micros() - s;
    if (!ok) {
      CPLOG_E("BENCH", "fsops: an operation failed on %s (file %d)", path, _fs.i);
      _fs.removing = true;
      _fs.files = _fs.i + 1;
      _fs.i = 0;
      return;
    }
  } else {
    const uint32_t s = micros();
    fs.remove(path);
    t[5] = micros() - s;
  }
  for (int k = 0; k < FsOps::OPS; k++) {
    _fs.sum[k] += t[k];
    if (t[k] > _fs.max[k]) _fs.max[k] = t[k];
  }
  _fs.n++;
  _fs.i++;
  const bool groupEnd = _fs.n == 16 || _fs.i >= _fs.files;
  if (groupEnd) {
    if (!_fs.removing) {
      CPLOG_I("BENCH", "  %5d | %6.1f / %6.1f | %5.1f / %5.1f | %6.1f / %6.1f | %6.1f / %6.1f | %6.1f / %6.1f | read %6.1f / %6.1f | read x2 %6.1f / %6.1f | %u KB used",
              _fs.i, _fs.sum[0] / 1000.0 / _fs.n, _fs.max[0] / 1000.0, _fs.sum[1] / 1000.0 / _fs.n,
              _fs.max[1] / 1000.0, _fs.sum[2] / 1000.0 / _fs.n, _fs.max[2] / 1000.0, _fs.sum[3] / 1000.0 / _fs.n,
              _fs.max[3] / 1000.0, _fs.sum[4] / 1000.0 / _fs.n, _fs.max[4] / 1000.0, _fs.sum[6] / 1000.0 / _fs.n,
              _fs.max[6] / 1000.0, _fs.sum[7] / 1000.0 / _fs.n, _fs.max[7] / 1000.0,
              (unsigned)(cpadv::dataFs().usedBytes() / 1024));
      CPLOG_I("BENCH", "  %5d | POSIX read %6.1f / %6.1f | POSIX write %6.1f / %6.1f ms", _fs.i, _fs.sum[8] / 1000.0 / _fs.n,
              _fs.max[8] / 1000.0, _fs.sum[9] / 1000.0 / _fs.n, _fs.max[9] / 1000.0);
    } else {
      CPLOG_I("BENCH", "  remove %5d | %6.1f / %6.1f ms | %u KB used", _fs.i, _fs.sum[5] / 1000.0 / _fs.n,
              _fs.max[5] / 1000.0, (unsigned)(cpadv::dataFs().usedBytes() / 1024));
    }
    memset(_fs.sum, 0, sizeof(_fs.sum));
    memset(_fs.max, 0, sizeof(_fs.max));
    _fs.n = 0;
  }
  if (_fs.i >= _fs.files) {
    if (!_fs.removing) {
      _fs.removing = true;
      _fs.i = 0;
    } else {
      _fs.running = false;
      free(_fs.data);
      _fs.data = nullptr;
      if (cpadv::dataFs().exists("/nftd")) cpadv::dataFs().rmdir("/nftd");
      CPLOG_I("BENCH", "fsops done, data partition %u KB used", (unsigned)(cpadv::dataFs().usedBytes() / 1024));
    }
  }
}
