#if CPADV_DB_TEST || defined(DBPROBE_HOST)
#include "DbVfsMeter.h"
#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

namespace {

sqlite3_vfs* g_base = nullptr;
sqlite3_vfs g_vfs;
DbVfsMeter::Clock g_clock = nullptr;
DbVfsMeter::Hook g_enter = nullptr, g_leave = nullptr;
DbVfsMeter::Counters g_count;
bool g_repair = true;
bool g_answersEverything = false;   // seen at the first open
constexpr int UNKNOWN_FILE_CONTROL = 0x6B7A0001;   // no VFS defines this opcode
bool g_truncateIgnored = false;
DbVfsMeter::Failure g_failure;

// One file: our header, then the base VFS's own file object
struct MeterFile {
  sqlite3_file base;
  sqlite3_file* real;
  char name[40];             // end of the path, for the failure report
};

const char* nameOf(sqlite3_file* f) { return reinterpret_cast<MeterFile*>(f)->name; }

// Keeps the first failure only: the later ones usually follow from it
int check(int rc, const char* op, const char* file, long long offset, int amount) {
  if (rc != SQLITE_OK && rc != SQLITE_IOERR_SHORT_READ && !g_failure.happened) {   // a short read of a new file is normal
    g_failure.happened = true;
    snprintf(g_failure.op, sizeof(g_failure.op), "%s", op);
    snprintf(g_failure.file, sizeof(g_failure.file), "%s", file);
    g_failure.offset = offset;
    g_failure.amount = amount;
    g_failure.rc = rc;
    g_failure.err = errno;
  }
  return rc;
}

// Times one operation and runs it inside the guard
class Op {
public:
  explicit Op(uint32_t& us) : _us(us), _t0(g_clock ? g_clock() : 0) { if (g_enter) g_enter(); }
  ~Op() {
    if (g_leave) g_leave();
    if (g_clock) _us += g_clock() - _t0;
  }
private:
  uint32_t& _us;
  uint32_t _t0;
};

sqlite3_file* real(sqlite3_file* f) { return reinterpret_cast<MeterFile*>(f)->real; }

int mClose(sqlite3_file* f) {
  Op op(g_count.usOpen);
  return real(f)->pMethods->xClose(real(f));
}
int mRead(sqlite3_file* f, void* buf, int n, sqlite3_int64 off) {
  g_count.reads++;
  g_count.readBytes += (uint32_t)n;
  Op op(g_count.usRead);
  errno = 0;
  return check(real(f)->pMethods->xRead(real(f), buf, n, off), "read", nameOf(f), off, n);
}
int mWrite(sqlite3_file* f, const void* buf, int n, sqlite3_int64 off) {
  g_count.writes++;
  g_count.writeBytes += (uint32_t)n;
  Op op(g_count.usWrite);
  errno = 0;
  return check(real(f)->pMethods->xWrite(real(f), buf, n, off), "write", nameOf(f), off, n);
}
int mTruncate(sqlite3_file* f, sqlite3_int64 size) {
  g_count.truncates++;
  Op op(g_count.usTruncate);
  errno = 0;
  int rc = check(real(f)->pMethods->xTruncate(real(f), size), "truncate", nameOf(f), size, 0);
  sqlite3_int64 now = 0;
  if (rc == SQLITE_OK && real(f)->pMethods->xFileSize(real(f), &now) == SQLITE_OK && now > size) g_truncateIgnored = true;
  return rc;
}
int mSync(sqlite3_file* f, int flags) {
  g_count.syncs++;
  Op op(g_count.usSync);
  errno = 0;
  return check(real(f)->pMethods->xSync(real(f), flags), "sync", nameOf(f), 0, 0);
}
int mFileSize(sqlite3_file* f, sqlite3_int64* size) { return real(f)->pMethods->xFileSize(real(f), size); }
int mLock(sqlite3_file* f, int l) { return real(f)->pMethods->xLock(real(f), l); }
int mUnlock(sqlite3_file* f, int l) { return real(f)->pMethods->xUnlock(real(f), l); }
int mCheckLock(sqlite3_file* f, int* out) { return real(f)->pMethods->xCheckReservedLock(real(f), out); }
int mControl(sqlite3_file* f, int op, void* arg) {
  if (g_repair && g_answersEverything) return SQLITE_NOTFOUND;   // its "handled" means nothing: let SQLite do the work
  return real(f)->pMethods->xFileControl(real(f), op, arg);
}
int mSector(sqlite3_file* f) { return real(f)->pMethods->xSectorSize(real(f)); }
int mCharacteristics(sqlite3_file* f) { return real(f)->pMethods->xDeviceCharacteristics(real(f)); }

const sqlite3_io_methods g_methods = {
  1,   // version 1: no shared memory (WAL is left out of this SQLite build anyway)
  mClose, mRead, mWrite, mTruncate, mSync, mFileSize, mLock, mUnlock, mCheckLock, mControl, mSector, mCharacteristics,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

int vOpen(sqlite3_vfs*, const char* name, sqlite3_file* f, int flags, int* outFlags) {
  MeterFile* m = reinterpret_cast<MeterFile*>(f);
  m->real = reinterpret_cast<sqlite3_file*>(m + 1);
  const char* slash = name ? strrchr(name, '/') : nullptr;
  snprintf(m->name, sizeof(m->name), "%s", name ? (slash ? slash + 1 : name) : "(temporary)");
  g_count.opens++;
  int rc;
  {
    Op op(g_count.usOpen);
    errno = 0;
    rc = check(g_base->xOpen(g_base, name, m->real, flags, outFlags), "open", m->name, 0, 0);
  }
  m->base.pMethods = rc == SQLITE_OK && m->real->pMethods ? &g_methods : nullptr;
  if (m->base.pMethods && m->real->pMethods->xFileControl &&
      m->real->pMethods->xFileControl(m->real, UNKNOWN_FILE_CONTROL, nullptr) == SQLITE_OK) {
    g_answersEverything = true;
  }
  return rc;
}

int vDelete(sqlite3_vfs*, const char* name, int dirSync) {
  g_count.deletes++;
  Op op(g_count.usDelete);
  return g_base->xDelete(g_base, name, dirSync);
}

int vAccess(sqlite3_vfs*, const char* name, int flags, int* out) {
  Op op(g_count.usOpen);
  return g_base->xAccess(g_base, name, flags, out);
}

int vFullPath(sqlite3_vfs*, const char* name, int n, char* out) { return g_base->xFullPathname(g_base, name, n, out); }
int vRandom(sqlite3_vfs*, int n, char* out) { return g_base->xRandomness(g_base, n, out); }
int vSleep(sqlite3_vfs*, int us) { return g_base->xSleep(g_base, us); }
int vTime(sqlite3_vfs*, double* t) { return g_base->xCurrentTime(g_base, t); }
int vError(sqlite3_vfs*, int n, char* out) { return g_base->xGetLastError ? g_base->xGetLastError(g_base, n, out) : 0; }

}  // namespace

bool DbVfsMeter::install(Clock micros, Hook enter, Hook leave, bool repairFileControl) {
  if (g_base) return true;
  g_repair = repairFileControl;
  g_answersEverything = false;
  g_truncateIgnored = false;
  g_failure = Failure();
  sqlite3_vfs* base = sqlite3_vfs_find(nullptr);
  if (!base) return false;
  g_base = base;
  g_clock = micros;
  g_enter = enter;
  g_leave = leave;
  g_count = Counters();
  memset(&g_vfs, 0, sizeof(g_vfs));
  g_vfs.iVersion = 1;
  g_vfs.szOsFile = (int)sizeof(MeterFile) + base->szOsFile;
  g_vfs.mxPathname = base->mxPathname;
  g_vfs.zName = "meter";
  g_vfs.xOpen = vOpen;
  g_vfs.xDelete = vDelete;
  g_vfs.xAccess = vAccess;
  g_vfs.xFullPathname = vFullPath;
  g_vfs.xRandomness = vRandom;
  g_vfs.xSleep = vSleep;
  g_vfs.xCurrentTime = vTime;
  g_vfs.xGetLastError = vError;
  if (sqlite3_vfs_register(&g_vfs, 1) != SQLITE_OK) {
    g_base = nullptr;
    return false;
  }
  return true;
}

void DbVfsMeter::uninstall() {
  if (!g_base) return;
  sqlite3_vfs_unregister(&g_vfs);
  g_base = nullptr;
  g_enter = g_leave = nullptr;
}

bool DbVfsMeter::installed() { return g_base != nullptr; }
bool DbVfsMeter::fileControlAnswersEverything() { return g_answersEverything; }
bool DbVfsMeter::truncateIgnored() { return g_truncateIgnored; }
DbVfsMeter::Failure DbVfsMeter::firstFailure() { return g_failure; }

DbVfsMeter::Counters DbVfsMeter::take() {
  Counters c = g_count;
  g_count = Counters();
  return c;
}
#endif  // CPADV_DB_TEST || DBPROBE_HOST
