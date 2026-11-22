#pragma once
// Device benches of the console (`bench`): the measures MESSAGING-12 rests on (docs/storage/NODES.md, step 3).
//   bench store [dir=root|sub] [task=0|1] [mhz=80|240]   the node archive's storage on the data partition
//   bench ecdh [n=16]                                      MeshCore's shared secret, at 240 then 80 MHz
//   bench clean                                            removes what an interrupted bench left
// The store bench runs one storage operation per pass of loop() (task=0: the radio is served between them) or in a
// task of its own (task=1). Either way loop() measures its own longest pause and the radio counters, so the report
// says what the firmware felt.
#include <stdint.h>

namespace mesh { class RNG; }
class StoreBench;

class DeviceBench {
public:
  void command(const char* args);
  void loop();                      // from loop(): runs a step (task=0) and watches the pauses
  bool running() const { return _bench != nullptr || _fs.running; }
  void setRng(mesh::RNG* rng) { _rng = rng; }

private:
  void startStore(const char* args);
  void finishStore();
  void ecdh(const char* args);
  void startFsOps(const char* args);
  void stepFsOps();
  void clean();
  static void task(void* self);

  StoreBench* _bench = nullptr;
  mesh::RNG* _rng = nullptr;
  bool _inTask = false;
  volatile bool _taskDone = false;
  uint32_t _cpuBefore = 0;
  uint32_t _lastLoopMs = 0, _maxLoopGapMs = 0;
  uint32_t _rxBefore = 0, _errBefore = 0;
  char _prefix[16] = "/nbt";
  // bench fsops: raw LittleFS operations against the number of files
  struct FsOps {
    bool running = false;
    int files = 0, i = 0, size = 4048;
    char prefix[16] = "/nft";
    bool removing = false;
    static constexpr int OPS = 10;  // create+write+close, stat, open r+ write 16 B, rename, rename back, remove,
                                    // read whole (1 open), read header + rest (2 opens), POSIX read, POSIX write
    uint64_t sum[OPS] = {};
    uint32_t max[OPS] = {};
    int n = 0;
    uint8_t* data = nullptr;     // 2 x 4 KB, only while the bench runs
  } _fs;
};
