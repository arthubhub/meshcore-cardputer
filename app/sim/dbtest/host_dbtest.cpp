// The SQLite trial (app/device/DbProbe) on a PC, with the SQLite sources and settings of the device library: checks the
// schema, the trial and its diagnostics before the Cardputer runs them. Times are the PC's; heap figures do not exist.
//   make -C app/sim dbtest            (needs `pio pkg install -e cardputer_adv_dual_dbtest`)
//   build/dbtest/dbtest [contacts] [messages] [key=value...]   keys: page cache journal sync batch
// Then it checks the repair of the ESP32 VFS's file control (DbVfsMeter): with a VFS that answers SQLITE_OK to every
// request, the settings must fail without the repair and work with it.
#include "../../device/DbProbe.h"
#include "../../device/DbVfsMeter.h"
#include <sqlite3.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

// A VFS with the faults of the ESP32 one (siara-cc/Sqlite3Esp32 2.5, esp32.cpp): xFileControl answers SQLITE_OK to every
// request, and xTruncate does nothing but answers SQLITE_OK. Everything else is the PC's "unix" VFS.
namespace stub {
sqlite3_vfs* g_unix = nullptr;
sqlite3_vfs g_vfs;

int answerEverything(sqlite3_file*, int, void*) { return SQLITE_OK; }
int truncateNothing(sqlite3_file*, sqlite3_int64) { return SQLITE_OK; }

// The unix VFS uses different methods for different files (database, journal): one modified copy per original
struct Methods { const sqlite3_io_methods* original; sqlite3_io_methods copy; };
Methods g_methods[8];
int g_count = 0;

const sqlite3_io_methods* esp32Like(const sqlite3_io_methods* original) {
  for (int i = 0; i < g_count; i++) if (g_methods[i].original == original) return &g_methods[i].copy;
  if (g_count == 8) return original;
  Methods& m = g_methods[g_count++];
  m.original = original;
  m.copy = *original;
  m.copy.iVersion = 1;
  m.copy.xFileControl = answerEverything;
  m.copy.xTruncate = truncateNothing;
  return &m.copy;
}

int open(sqlite3_vfs*, const char* name, sqlite3_file* f, int flags, int* out) {
  int rc = g_unix->xOpen(g_unix, name, f, flags, out);
  if (rc == SQLITE_OK && f->pMethods) f->pMethods = esp32Like(f->pMethods);
  return rc;
}

void install() {
  if (g_unix) {                      // already filled: copying it again while it is in SQLite's list would break the list
    sqlite3_vfs_register(&g_vfs, 1);
    return;
  }
  g_unix = sqlite3_vfs_find("unix");
  g_vfs = *g_unix;
  g_vfs.zName = "esp32-like";
  g_vfs.xOpen = open;
  sqlite3_vfs_register(&g_vfs, 1);
}
}  // namespace stub

class HostDb : public DbProbeHost {
public:
  uint32_t ms() override { return (uint32_t)(us() / 1000); }
  uint32_t micros() override { return (uint32_t)us(); }
  uint32_t freeHeap() override { return 0; }
  uint32_t largestBlock() override { return 0; }
  uint32_t systemMinHeap() override { return 0; }
  uint32_t fsUsedKB() override { return 0; }
  uint32_t fsFreeKB() override { return 0; }
  void service() override { services++; }
  void log(bool error, const char* line) override { printf("[DB] %s%s\n", error ? "ERROR " : "", line); }
  void removeFile(const char* path) override { remove(path); }
  void onSqliteReady() override { if (esp32LikeVfs) stub::install(); }
  int services = 0;
  bool esp32LikeVfs = false;

private:
  static uint64_t us() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
  }
};

int main(int argc, char** argv) {
  std::ifstream f("../../docs/storage/schema.sql");
  std::stringstream sql;
  sql << f.rdbuf();
  const std::string schema = sql.str();
  if (schema.empty()) { printf("cannot read docs/storage/schema.sql\n"); return 1; }

  HostDb host;
  DbProbe probe(host);
  DbProbeOptions o;
  o.path = "build/dbtest/dbtest.db";
  o.schema = schema.c_str();
  static std::string journal, sync;
  int positional = 0;
  for (int i = 1; i < argc; i++) {
    const char* eq = strchr(argv[i], '=');
    if (!eq) {
      (positional++ == 0 ? o.contacts : o.messages) = atoi(argv[i]);
      continue;
    }
    std::string key(argv[i], eq - argv[i]), value(eq + 1);
    if (key == "page") o.pageSize = atoi(value.c_str());
    else if (key == "cache") o.cacheKB = atoi(value.c_str());
    else if (key == "batch") o.batch = atoi(value.c_str());
    else if (key == "journal") { journal = value; o.journal = journal.c_str(); }
    else if (key == "sync") { sync = value; o.sync = sync.c_str(); }
    else { printf("unknown setting %s\n", argv[i]); return 1; }
  }
  DbProbeResult r;
  bool ok = probe.run(o, r);

  // The trial must also notice a broken schema and stop cleanly
  DbProbeOptions bad = o;
  bad.contacts = 20;
  bad.messages = 20;
  bad.schema = "CREATE TABLE contacts(id INTEGER PRIMARY KEY);";   // no such columns: inserting must fail
  DbProbeResult rb;
  bool badOk = probe.run(bad, rb);
  bool badCaught = !badOk && std::string(rb.failedStep) == "contacts";

  // The fault of the ESP32 VFS, reproduced: settings ignored without the repair, applied with it
  DbProbeOptions small = o;
  small.contacts = 30;
  small.messages = 30;
  host.esp32LikeVfs = true;
  small.repairFileControl = false;
  DbProbeResult rf;
  bool faultShows = !probe.run(small, rf) && std::string(rf.failedStep) == "open+settings";
  small.repairFileControl = true;
  DbProbeResult rr;
  bool repaired = probe.run(small, rr);
  bool truncateSeen = DbVfsMeter::truncateIgnored();   // journal=truncate on a VFS that ignores truncations
  host.esp32LikeVfs = false;
  FILE* left = fopen(o.path, "rb");
  if (left) fclose(left);

  printf("== dbtest: trial %s, broken schema %s, ESP32-like VFS: fault %s, repair %s, ignored truncation %s, file %s, "
         "service called %d times\n", ok ? "ok" : "FAILED", badCaught ? "caught" : "NOT CAUGHT", faultShows ? "reproduced" : "NOT REPRODUCED",
         repaired ? "works" : "FAILED", truncateSeen ? "detected" : "NOT DETECTED", left ? "LEFT BEHIND" : "deleted", host.services);
  return ok && badCaught && faultShows && repaired && truncateSeen && !left ? 0 : 1;
}
