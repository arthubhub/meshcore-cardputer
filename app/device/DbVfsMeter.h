#pragma once
// Where SQLite's time goes on a file system (SQLite trial, docs/storage). A SQLite VFS that wraps the default one
// ("esp32" on the Cardputer, "unix" on a PC) and counts, with their time: file opens and deletes, reads, writes, syncs
// and truncations. It can also run each file operation inside a guard (the SD card's bus lock, so the radio finds a
// clean bus between two operations). Portable: SQLite's API only.
//
// It also repairs the file control of a VFS that answers SQLITE_OK to every request. SQLite asks the VFS about each
// PRAGMA first (SQLITE_FCNTL_PRAGMA) and, on SQLITE_OK, takes the PRAGMA as handled by the VFS and does nothing else.
// The ESP32 VFS of siara-cc/Sqlite3Esp32 2.5 does exactly that (esp32.cpp, ESP32FileControl: `return SQLITE_OK;`), so
// every PRAGMA was silently ignored on the Cardputer: page size, cache, journal, synchronous, quick_check (seen in the
// trial of 2026-11-09). Such a VFS is recognised at the first file open, with a request no VFS knows, and the meter then
// answers SQLITE_NOTFOUND in its place, as SQLite's VFS contract requires.
#include <stdint.h>

class DbVfsMeter {
public:
  struct Counters {
    uint32_t opens = 0, deletes = 0, reads = 0, writes = 0, syncs = 0, truncates = 0;
    uint32_t readBytes = 0, writeBytes = 0;
    uint32_t usOpen = 0, usDelete = 0, usRead = 0, usWrite = 0, usSync = 0, usTruncate = 0;
  };
  using Clock = uint32_t (*)();       // microseconds
  using Hook = void (*)();

  // Registers the meter as SQLite's default VFS, on top of the current default. Call after sqlite3_initialize.
  // repairFileControl = false keeps the base VFS's answers, to show the failure it causes.
  static bool install(Clock micros, Hook enter = nullptr, Hook leave = nullptr, bool repairFileControl = true);
  static void uninstall();
  static bool installed();
  // The counters since the previous call, then zero again
  static Counters take();
  // The base VFS answered SQLITE_OK to an unknown file control request (at least one file opened since install)
  static bool fileControlAnswersEverything();
  // A truncation reported as done left the file longer (the ESP32 VFS's xTruncate does nothing): journal_mode=truncate
  // then leaves a valid journal behind, which a later open would roll back. Use persist or delete there.
  static bool truncateIgnored();

  // The first failed file operation since install: what, on which file, where, and errno
  struct Failure {
    bool happened = false;
    char op[10] = "";
    char file[40] = "";
    long long offset = 0;
    int amount = 0;
    int rc = 0;
    int err = 0;              // errno right after the call
  };
  static Failure firstFailure();
};
