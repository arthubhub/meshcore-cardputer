#if CPADV_DB_TEST || defined(DBPROBE_HOST)
#include "DbProbe.h"
#include "DbVfsMeter.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr int MESSAGES_PER_CONVERSATION = 250;
DbProbeHost* g_clockHost = nullptr;   // DbVfsMeter takes a plain function
uint32_t meterClock() { return g_clockHost ? g_clockHost->micros() : 0; }
void meterEnter() { if (g_clockHost) g_clockHost->ioEnter(); }
void meterLeave() { if (g_clockHost) g_clockHost->ioLeave(); }
}  // namespace

uint32_t DbProbe::random() {
  _seed ^= _seed << 13;
  _seed ^= _seed >> 17;
  _seed ^= _seed << 5;
  return _seed;
}

void DbProbe::sample() {
  const uint32_t heap = _h.freeHeap(), block = _h.largestBlock();
  if (heap && (!_r->minFreeHeap || heap < _r->minFreeHeap)) _r->minFreeHeap = heap;
  if (block && (!_r->minLargestBlock || block < _r->minLargestBlock)) _r->minLargestBlock = block;
}

void DbProbe::serviceNow() {
  const uint32_t now = _h.ms();
  if (now - _lastService > _r->maxPauseMs) _r->maxPauseMs = now - _lastService;
  sample();
  _h.service();
  _lastService = _h.ms();
}

void DbProbe::endTransaction(uint32_t startMs) {
  const uint32_t ms = _h.ms() - startMs;
  if (ms > _r->maxTxnMs) _r->maxTxnMs = ms;
  _txnUsTotal += (uint64_t)ms * 1000;
  _txns++;
  serviceNow();
}

void DbProbe::stepLog(const char* name, uint32_t startMs, const char* detail) {
  char line[360];
  const DbVfsMeter::Counters c = DbVfsMeter::take();
  snprintf(line, sizeof(line), "step %-14s ok %6u ms | heap %u KB, largest %u KB | sqlite %u KB (peak %u KB) | io open %u del %u "
           "read %u (%u KB, %u ms) write %u (%u KB, %u ms) sync %u (%u ms) trunc %u (%u ms)%s%s",
           name, (unsigned)(_h.ms() - startMs), (unsigned)(_h.freeHeap() / 1024), (unsigned)(_h.largestBlock() / 1024),
           (unsigned)(sqlite3_memory_used() / 1024), (unsigned)(sqlite3_memory_highwater(0) / 1024), (unsigned)c.opens,
           (unsigned)c.deletes, (unsigned)c.reads, (unsigned)(c.readBytes / 1024), (unsigned)(c.usRead / 1000), (unsigned)c.writes,
           (unsigned)(c.writeBytes / 1024), (unsigned)(c.usWrite / 1000), (unsigned)c.syncs, (unsigned)(c.usSync / 1000),
           (unsigned)c.truncates, (unsigned)(c.usTruncate / 1000), detail && *detail ? " | " : "", detail ? detail : "");
  _h.log(false, line);
  sample();
}

bool DbProbe::fail(const char* what) {
  char line[320];
  // SQLite's message first: `what` may be a whole schema, only its start is kept
  snprintf(line, sizeof(line), "step %-14s FAILED: %s (code %d) | in: %.80s", _step, _db ? sqlite3_errmsg(_db) : "no database",
           _db ? sqlite3_extended_errcode(_db) : 0, what);
  _h.log(true, line);
  const DbVfsMeter::Failure io = DbVfsMeter::firstFailure();
  if (io.happened) {
    snprintf(line, sizeof(line), "  first failed file operation: %s on %s, offset %lld, %d bytes -> SQLite code %d, errno %d (%s)", io.op,
             io.file, io.offset, io.amount, io.rc, io.err, io.err ? strerror(io.err) : "not set");
    _h.log(true, line);
  }
  snprintf(_r->failedStep, sizeof(_r->failedStep), "%s", _step);
  return false;
}

bool DbProbe::exec(const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(_db, sql, nullptr, nullptr, &err) == SQLITE_OK) return true;
  sqlite3_free(err);
  return fail(sql);
}

bool DbProbe::execScript(const char* sql, int& statements) {
  statements = 0;
  const char* next = sql;
  while (next && *next) {
    sqlite3_stmt* st = nullptr;
    const char* start = next;
    if (sqlite3_prepare_v2(_db, start, -1, &st, &next) != SQLITE_OK) {
      while (*start == ' ' || *start == '\n' || *start == '-') {   // point at the statement, not at its comment
        if (*start == '-') { while (*start && *start != '\n') start++; } else start++;
      }
      return fail(start);
    }
    if (!st) continue;   // only spaces or comments left
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {}
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return fail(start);
    statements++;
  }
  return true;
}

bool DbProbe::prepare(const char* sql, sqlite3_stmt** st) {
  return sqlite3_prepare_v2(_db, sql, -1, st, nullptr) == SQLITE_OK || fail(sql);
}

bool DbProbe::done(sqlite3_stmt* st) {
  int rc;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {}
  sqlite3_reset(st);
  return rc == SQLITE_DONE || fail(sqlite3_sql(st));
}

// One value from a query. When there is none, the log says why: the first trial lost that information.
bool DbProbe::scalar(const char* sql, int64_t& out) {
  sqlite3_stmt* st = nullptr;
  int rc = sqlite3_prepare_v2(_db, sql, -1, &st, nullptr);
  int step = rc == SQLITE_OK && st ? sqlite3_step(st) : rc;
  if (step == SQLITE_ROW) out = sqlite3_column_int64(st, 0);
  char line[240];
  if (step != SQLITE_ROW) {
    snprintf(line, sizeof(line), "  no value from \"%.60s\": prepare %d, step %d (%s)%s", sql, rc, step, sqlite3_errmsg(_db),
             rc == SQLITE_OK && !st ? ", nothing to run: unknown or left-out statement" : "");
    _h.log(true, line);
  }
  sqlite3_finalize(st);
  return step == SQLITE_ROW;
}

bool DbProbe::text(const char* sql, char* out, size_t n) {
  sqlite3_stmt* st = nullptr;
  int rc = sqlite3_prepare_v2(_db, sql, -1, &st, nullptr);
  int step = rc == SQLITE_OK && st ? sqlite3_step(st) : rc;
  if (step == SQLITE_ROW) snprintf(out, n, "%s", (const char*)sqlite3_column_text(st, 0));
  else snprintf(out, n, "(none: prepare %d, step %d, %s)", rc, step, sqlite3_errmsg(_db));
  sqlite3_finalize(st);
  return step == SQLITE_ROW;
}

// How the library was really built: the trial must know it, not guess it
void DbProbe::logBuildOptions() {
  char line[240];
  size_t n = (size_t)snprintf(line, sizeof(line), "build:");
  for (int i = 0; const char* opt = sqlite3_compileoption_get(i); i++) {
    if (n + strlen(opt) + 2 >= sizeof(line)) {
      _h.log(false, line);
      n = (size_t)snprintf(line, sizeof(line), "build:");
    }
    n += (size_t)snprintf(line + n, sizeof(line) - n, " %s", opt);
  }
  _h.log(false, line);
}

// SQLite's global settings: only allowed before sqlite3_initialize (run() shuts SQLite down at the end)
bool DbProbe::configure(const DbProbeOptions& o) {
  _step = "configure";
  const uint32_t t = _h.ms();
  int rcMem = sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 1);           // the library turns the counters off by default
  int rcLook = sqlite3_config(SQLITE_CONFIG_LOOKASIDE, o.lookasideSlot, o.lookasideCount);
  if (sqlite3_initialize() != SQLITE_OK) return fail("sqlite3_initialize");
  sqlite3_memory_highwater(1);                                       // this run's peak only
  logBuildOptions();
  _h.onSqliteReady();
  g_clockHost = &_h;
  const bool meter = DbVfsMeter::install(meterClock, meterEnter, meterLeave, o.repairFileControl);
  char detail[160];
  snprintf(detail, sizeof(detail), "SQLite %s, memory stats %s, lookaside %dx%d%s, file meter %s, VFS %s", sqlite3_libversion(),
           rcMem == SQLITE_OK ? "on" : "unavailable", o.lookasideSlot, o.lookasideCount, rcLook == SQLITE_OK ? "" : " (refused)",
           meter ? "on" : "FAILED", sqlite3_vfs_find(nullptr) ? sqlite3_vfs_find(nullptr)->zName : "?");
  stepLog(_step, t, detail);
  return true;
}

// Writes a setting, then reads what SQLite really keeps. `mustRead`: no value back is a failure.
bool DbProbe::setting(const char* name, const char* value, bool mustRead) {
  char sql[64], got[96];
  snprintf(sql, sizeof(sql), "PRAGMA %s=%s", name, value);
  sqlite3_stmt* st = nullptr;
  int rc = sqlite3_prepare_v2(_db, sql, -1, &st, nullptr);
  int step = rc == SQLITE_OK && st ? sqlite3_step(st) : rc;
  sqlite3_finalize(st);
  snprintf(sql, sizeof(sql), "PRAGMA %s", name);
  const bool read = text(sql, got, sizeof(got));
  char line[240];
  snprintf(line, sizeof(line), "  setting %-13s asked %-9s -> %s (write: prepare %d, step %d)", name, value, got, rc, step);
  _h.log(!read || (rc != SQLITE_OK), line);
  if (mustRead && !read) return fail(sql);
  return true;
}

bool DbProbe::open(const DbProbeOptions& o, bool create) {
  _step = create ? "open+settings" : "reopen";
  const uint32_t t = _h.ms();
  if (sqlite3_open(o.path, &_db) != SQLITE_OK) return fail("sqlite3_open");
  if (create) {
    char line[200];
    snprintf(line, sizeof(line), "  file control of the base VFS: %s",
             !DbVfsMeter::fileControlAnswersEverything() ? "answers correctly"
             : o.repairFileControl ? "answers SQLITE_OK to everything (PRAGMAs would be ignored): repaired by the meter"
                                   : "answers SQLITE_OK to everything, NOT repaired (repair=0): PRAGMAs will be ignored");
    _h.log(DbVfsMeter::fileControlAnswersEverything() && !o.repairFileControl, line);
  }
  char v[16];
  bool ok = true;
  if (create) {
    snprintf(v, sizeof(v), "%d", o.pageSize);
    ok = setting("page_size", v, true);
  }
  snprintf(v, sizeof(v), "-%d", o.cacheKB);
  ok = ok && setting("cache_size", v, false) && setting("journal_mode", o.journal, false) && setting("synchronous", o.sync, false) &&
       setting("foreign_keys", "ON", false) && setting("temp_store", "MEMORY", false) && setting("locking_mode", "EXCLUSIVE", false);
  if (!ok) return false;
  stepLog(_step, t, "");
  return true;
}

bool DbProbe::fillContacts(const DbProbeOptions& o) {
  _step = "contacts";
  const uint32_t t = _h.ms();
  sqlite3_stmt *contact = nullptr, *position = nullptr, *link = nullptr;
  bool ok = prepare("INSERT INTO contacts(pub_key,name,name_fold,type,favourite,in_ram,first_heard_at,last_heard_at,"
                    "last_advert_time,out_path_wire,out_path) VALUES(?,?,?,?,?,?,?,?,?,?,?)", &contact) &&
            prepare("INSERT INTO positions(contact_id,lat_e6,lon_e6,time,received_at,source) VALUES(?,?,?,?,?,1)", &position) &&
            prepare("UPDATE contacts SET position_id=? WHERE id=?", &link);
  uint32_t first = 0;
  for (int i = 0; ok && i < o.contacts; i += o.batch) {
    const uint32_t txStart = _h.ms();
    ok = exec("BEGIN");
    for (int k = i; ok && k < i + o.batch && k < o.contacts; k++) {
      uint8_t key[32];
      for (int b = 0; b < 32; b += 4) { uint32_t v = random(); memcpy(key + b, &v, 4); }
      char name[32], fold[32];
      snprintf(name, sizeof(name), k % 7 ? "Node %d" : "Élodie-%d", k);
      snprintf(fold, sizeof(fold), k % 7 ? "node %d" : "elodie-%d", k);
      const uint8_t path[3] = {(uint8_t)random(), (uint8_t)random(), (uint8_t)random()};
      const int64_t at = 1793540000 + k;
      sqlite3_bind_blob(contact, 1, key, 32, SQLITE_TRANSIENT);
      sqlite3_bind_text(contact, 2, name, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(contact, 3, fold, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(contact, 4, k % 3 ? 1 : 2);
      sqlite3_bind_int(contact, 5, k % 50 == 0);
      sqlite3_bind_int(contact, 6, k >= o.contacts - 80);
      sqlite3_bind_int64(contact, 7, at);
      sqlite3_bind_int64(contact, 8, at);
      sqlite3_bind_int64(contact, 9, at);
      sqlite3_bind_int(contact, 10, 3);
      sqlite3_bind_blob(contact, 11, path, 3, SQLITE_TRANSIENT);
      ok = done(contact);
      const sqlite3_int64 id = sqlite3_last_insert_rowid(_db);
      sqlite3_bind_int64(position, 1, id);
      sqlite3_bind_int(position, 2, 43600000 + (int)(random() % 100000));
      sqlite3_bind_int(position, 3, 1440000 + (int)(random() % 100000));
      sqlite3_bind_int64(position, 4, at);
      sqlite3_bind_int64(position, 5, at);
      ok = ok && done(position);
      sqlite3_bind_int64(link, 1, sqlite3_last_insert_rowid(_db));
      sqlite3_bind_int64(link, 2, id);
      ok = ok && done(link);
    }
    ok = ok && exec("COMMIT");
    endTransaction(txStart);
    if (i == 0) {   // the first transaction alone: its file operations show the cost of one commit
      first = _h.ms() - txStart;
      const DbVfsMeter::Counters c = DbVfsMeter::take();
      char line[240];
      snprintf(line, sizeof(line), "  first transaction: %u ms | open %u del %u read %u write %u (%u KB, %u ms) sync %u (%u ms) trunc %u",
               (unsigned)first, (unsigned)c.opens, (unsigned)c.deletes, (unsigned)c.reads, (unsigned)c.writes,
               (unsigned)(c.writeBytes / 1024), (unsigned)(c.usWrite / 1000), (unsigned)c.syncs, (unsigned)(c.usSync / 1000),
               (unsigned)c.truncates);
      _h.log(false, line);
    }
  }
  sqlite3_finalize(contact);
  sqlite3_finalize(position);
  sqlite3_finalize(link);
  if (!ok) return false;
  int64_t n = -1;
  if (!scalar("SELECT count(*) FROM contacts", n) || n != o.contacts) return fail("contact count after inserting");
  char detail[64];
  snprintf(detail, sizeof(detail), "%d contacts + positions, %u transactions", o.contacts, (unsigned)_txns);
  stepLog(_step, t, detail);
  return true;
}

bool DbProbe::fillMessages(const DbProbeOptions& o) {
  _step = "messages";
  const uint32_t t = _h.ms();
  const int conversations = (o.messages + MESSAGES_PER_CONVERSATION - 1) / MESSAGES_PER_CONVERSATION;
  bool ok = exec("BEGIN");
  for (int c = 1; ok && c <= conversations && c <= o.contacts; c++) {
    char sql[120];
    snprintf(sql, sizeof(sql), "INSERT INTO conversations(contact_id,name,last_at) VALUES(%d,'Conversation %d',0)", c, c);
    ok = exec(sql);
  }
  ok = ok && exec("COMMIT");
  sqlite3_stmt* msg = nullptr;
  ok = ok && prepare("INSERT INTO messages(conversation_id,outgoing,sender_id,text,sent_time,received_at,state,snr_x4,rssi,hops,"
                     "route_type,path_kind,path_wire,path) VALUES(?,?,?,?,?,?,?,?,?,?,1,1,?,?)", &msg);
  const uint32_t before = _txns;
  for (int i = 0; ok && i < o.messages; i += o.batch) {
    const uint32_t txStart = _h.ms();
    ok = exec("BEGIN");
    for (int k = i; ok && k < i + o.batch && k < o.messages; k++) {
      char text[96];
      snprintf(text, sizeof(text), "Message %d: rendez-vous au parc vers 18h, la portée est bonne aujourd'hui", k);
      const uint8_t path[4] = {(uint8_t)random(), (uint8_t)random(), (uint8_t)random(), (uint8_t)random()};
      const int conv = 1 + k % conversations;
      sqlite3_bind_int(msg, 1, conv);
      sqlite3_bind_int(msg, 2, k % 4 == 0);
      sqlite3_bind_int(msg, 3, conv);
      sqlite3_bind_text(msg, 4, text, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(msg, 5, 1793541000 + k);
      sqlite3_bind_int64(msg, 6, 1793541000 + k);
      sqlite3_bind_int(msg, 7, k % 4 == 0 ? 4 : 0);
      sqlite3_bind_int(msg, 8, -30 + (int)(random() % 60));
      sqlite3_bind_int(msg, 9, -110 + (int)(random() % 30));
      sqlite3_bind_int(msg, 10, 4);
      sqlite3_bind_int(msg, 11, 4);
      sqlite3_bind_blob(msg, 12, path, 4, SQLITE_TRANSIENT);
      ok = done(msg);
    }
    ok = ok && exec("COMMIT");
    endTransaction(txStart);
  }
  sqlite3_finalize(msg);
  if (!ok) return false;
  int64_t n = -1;
  if (!scalar("SELECT count(*) FROM messages", n) || n != o.messages) return fail("message count after inserting");
  char detail[64];
  snprintf(detail, sizeof(detail), "%d messages in %d conversations, %u transactions", o.messages, conversations, (unsigned)(_txns - before));
  stepLog(_step, t, detail);
  return true;
}

// The queries the screens and the mesh need, each repeated, average and worst time
bool DbProbe::queries(const DbProbeOptions&) {
  struct Query { const char* name; const char* sql; int repeat; };
  static const Query Q[] = {
    {"q-hash-1byte", "SELECT id FROM contacts WHERE pub_key >= x'a1' AND pub_key < x'a2'", 50},   // a node from a path
    {"q-ram-leaves", "SELECT id FROM contacts WHERE in_ram=1 AND favourite=0 ORDER BY last_heard_at LIMIT 10", 50},
    {"q-name-start", "SELECT id FROM contacts WHERE name_fold GLOB 'node 1*' LIMIT 20", 20},
    {"q-name-any", "SELECT id FROM contacts WHERE instr(name_fold,'lodie') > 0 LIMIT 20", 10},
    {"q-last-msgs", "SELECT * FROM messages WHERE conversation_id=1 ORDER BY id DESC LIMIT 10", 50},
    {"q-position", "SELECT p.lat_e6, p.lon_e6, p.time, p.source FROM contacts c JOIN positions p ON p.id = c.position_id "
                   "WHERE c.id = 42", 50},
    {"q-conv-list", "SELECT id, name, unread, last_at, last_preview, last_outgoing, channel_id IS NOT NULL FROM conversations "
                    "ORDER BY last_at DESC LIMIT 8", 20},
  };
  for (const Query& q : Q) {
    _step = q.name;
    sqlite3_stmt* st = nullptr;
    if (!prepare(q.sql, &st)) return false;
    const uint32_t t = _h.ms();
    uint32_t worst = 0;
    int rows = 0;
    for (int i = 0; i < q.repeat; i++) {
      const uint32_t s = _h.micros();
      int rc;
      rows = 0;
      while ((rc = sqlite3_step(st)) == SQLITE_ROW) rows++;
      sqlite3_reset(st);
      if (rc != SQLITE_DONE) { sqlite3_finalize(st); return fail(q.sql); }
      if (_h.micros() - s > worst) worst = _h.micros() - s;
      if (i % 10 == 9) serviceNow();
    }
    sqlite3_finalize(st);
    char detail[80];
    snprintf(detail, sizeof(detail), "%d rows, avg %u us, worst %u us (x%d)", rows,
             (unsigned)((_h.ms() - t) * 1000 / q.repeat), (unsigned)worst, q.repeat);
    stepLog(q.name, t, detail);
  }
  return true;
}

bool DbProbe::checkAndReopen(const DbProbeOptions& o) {
  _step = "quick_check";
  uint32_t t = _h.ms();
  char result[96];
  if (!text("PRAGMA quick_check", result, sizeof(result))) return fail(result);
  if (strcmp(result, "ok") != 0) return fail(result);
  int64_t pages = 0, size = 0;
  if (scalar("PRAGMA page_count", pages) && scalar("PRAGMA page_size", size)) _r->fileKB = (uint32_t)(pages * size / 1024);
  char detail[128];
  snprintf(detail, sizeof(detail), "%s, file %u KB", result, (unsigned)_r->fileKB);
  stepLog(_step, t, detail);
  serviceNow();

  _step = "close";
  t = _h.ms();
  if (sqlite3_close(_db) != SQLITE_OK) return fail("sqlite3_close (statements left open)");
  _db = nullptr;
  stepLog(_step, t, "");

  t = _h.ms();
  if (!open(o, false)) return false;
  _r->reopenMs = _h.ms() - t;
  _step = "reopen-count";
  t = _h.ms();
  int64_t n = -1;
  if (!scalar("SELECT count(*) FROM contacts", n) || n != o.contacts) return fail("contacts after reopening");
  char d2[48];
  snprintf(d2, sizeof(d2), "%lld contacts read back", (long long)n);
  stepLog(_step, t, d2);
  return true;
}

void DbProbe::cleanup(const DbProbeOptions& o) {
  if (_db) sqlite3_close_v2(_db);
  _db = nullptr;
  if (!o.keep) {
    _h.removeFile(o.path);
    char journal[128];
    snprintf(journal, sizeof(journal), "%s-journal", o.path);
    _h.removeFile(journal);
  }
  DbVfsMeter::uninstall();
  sqlite3_shutdown();   // lets the next run configure SQLite again
}

bool DbProbe::run(const DbProbeOptions& o, DbProbeResult& r) {
  r = DbProbeResult();
  _r = &r;
  _txns = 0;
  _txnUsTotal = 0;
  char line[400];
  const uint32_t heap0 = _h.freeHeap(), fs0 = _h.fsUsedKB(), fsFree = _h.fsFreeKB();
  r.systemMinBefore = _h.systemMinHeap();
  const uint32_t needKB = (uint32_t)(o.contacts * 0.4 + o.messages * 0.25) * 2 + 64;   // twice the expected size
  snprintf(line, sizeof(line), "SQLite trial: %d contacts, %d messages, %d per transaction, page %d B, cache %d KB, journal %s, "
           "sync %s, file %s | heap %u KB free, largest %u KB, lowest since boot %u KB | file system %u KB used, %u KB free",
           o.contacts, o.messages, o.batch, o.pageSize, o.cacheKB, o.journal, o.sync, o.path, (unsigned)(heap0 / 1024),
           (unsigned)(_h.largestBlock() / 1024), (unsigned)(r.systemMinBefore / 1024), (unsigned)fs0, (unsigned)fsFree);
  _h.log(false, line);
  _step = "space";
  if (fsFree && fsFree < needKB) {
    snprintf(line, sizeof(line), "step space          FAILED: %u KB free, the trial needs about %u KB: lower the counts", (unsigned)fsFree, (unsigned)needKB);
    _h.log(true, line);
    snprintf(r.failedStep, sizeof(r.failedStep), "space");
    return false;
  }
  _h.removeFile(o.path);   // left by an interrupted trial
  _lastService = _h.ms();
  sample();

  bool ok = configure(o) && open(o, true);
  if (ok) {
    _step = "schema";
    const uint32_t t = _h.ms();
    int statements = 0;
    ok = o.schema && execScript(o.schema, statements);
    if (ok) {
      int64_t tables = -1;
      scalar("SELECT count(*) FROM sqlite_master WHERE type='table'", tables);
      char d[64];
      snprintf(d, sizeof(d), "%d statements, %lld tables", statements, (long long)tables);
      stepLog(_step, t, d);
    } else if (!o.schema) {
      fail("no schema given");
    }
  }
  ok = ok && fillContacts(o) && fillMessages(o) && queries(o) && checkAndReopen(o);
  r.sqlitePeak = (uint32_t)sqlite3_memory_highwater(0);
  r.avgTxnUs = _txns ? (uint32_t)(_txnUsTotal / _txns) : 0;
  const uint32_t fs1 = _h.fsUsedKB();
  r.fsUsedDeltaKB = fs1 > fs0 ? fs1 - fs0 : 0;
  cleanup(o);
  r.systemMinAfter = _h.systemMinHeap();
  // The system's low-water mark counts since boot: it only tells about this trial if it went down during it
  const bool memoryMeasured = r.systemMinAfter && r.systemMinAfter < r.systemMinBefore;
  r.memoryOk = !memoryMeasured || r.systemMinAfter >= o.heapFloor;
  if (r.systemMinAfter && !memoryMeasured) {
    snprintf(line, sizeof(line), "  memory not measured by this trial: the lowest free heap (%u KB) was reached before it. "
             "Restart the Cardputer before a trial to measure it", (unsigned)(r.systemMinAfter / 1024));
    _h.log(false, line);
  }
  if (DbVfsMeter::truncateIgnored()) {
    _h.log(true, "  the base VFS ignores truncations (reported done, file left longer): journal_mode=truncate leaves a valid "
                 "journal behind, which a later open would roll back. Use journal=persist or journal=delete on it");
  }
  r.ok = ok && r.memoryOk;
  if (ok && !r.memoryOk) snprintf(r.failedStep, sizeof(r.failedStep), "memory");
  snprintf(line, sizeof(line), "RESULT %s%s | lowest free heap of the system %u KB before, %u KB after%s | sampled min %u KB (largest "
           "block %u KB) | sqlite peak %u KB | file %u KB (partition +%u KB) | txn avg %u ms, max %u ms | longest pause %u ms | reopen %u ms%s",
           r.ok ? "OK" : "FAILED at ", r.ok ? "" : r.failedStep, (unsigned)(r.systemMinBefore / 1024), (unsigned)(r.systemMinAfter / 1024),
           r.memoryOk ? "" : " (BELOW THE FLOOR)", (unsigned)(r.minFreeHeap / 1024), (unsigned)(r.minLargestBlock / 1024),
           (unsigned)(r.sqlitePeak / 1024), (unsigned)r.fileKB, (unsigned)r.fsUsedDeltaKB, (unsigned)(r.avgTxnUs / 1000),
           (unsigned)r.maxTxnMs, (unsigned)r.maxPauseMs, (unsigned)r.reopenMs, o.keep ? " | file kept" : " | file deleted");
  _h.log(!r.ok, line);
  return r.ok;
}
#endif  // CPADV_DB_TEST || DBPROBE_HOST
