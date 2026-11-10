# Keyboard firmware database (proposal, rejected on 2026-11-10)

> **Decision (2026-11-10):** SQLite is not used by the firmware. Trials on the Cardputer (end of this file) show too
> little free memory and slow writes, on the internal flash and on the SD card. The schema stays as the reference of
> what to store; MESSAGING-12 uses files.

`schema.sql` describes one SQLite database for everything the keyboard firmware stores: contacts without a size limit,
their dated positions, channels, conversations, messages with their whole route, packets heard, discovery results and
GPS tracks. Nothing uses it yet. Checked on a PC with SQLite 3.46: the schema loads, and the queries the screens need
use an index (looking a node up from 1 byte of its key, the next contacts to leave RAM, the last messages of a
conversation, the start of a name). 3,000 contacts with a position and 5,000 messages take about 1 MB.

## What lives where

| Data | Today | With the database |
|---|---|---|
| Contacts | `/contacts.cp`, 100 at most | `contacts`, no limit; MeshCore's 100 places hold the ones with `in_ram = 1` |
| Positions | in the contact, time in `PositionBook` | `positions`, every one, `contacts.position_id` the newest |
| Messages | 64 per conversation, 32 bytes of route | `messages`, whole route, kept by a budget |
| Packets | 48 in RAM, CSV on the SD card | `packets`, kept by a budget |
| Settings, identity | `/cpadv_settings.bin`, `/identity` | unchanged: needed before the database opens, and by the SD backup |

## On the device

- Library: an ESP32 port of SQLite on the LittleFS VFS. `PRAGMA page_size = 4096`, `cache_size = -32` (32 KB),
  `journal_mode = TRUNCATE` (WAL needs shared memory the VFS does not give), `synchronous = NORMAL`,
  `foreign_keys = ON`.
- Writes are grouped: one transaction every 5 seconds or every 10 packets, whichever comes first, and at once for an
  action of the user (favourite, delete, send). A power cut loses at most that interval, never the file.
- Budgets, applied once an hour: last 64 messages per conversation (today's value, can grow), last 2,000 packets,
  positions older than 30 days except the newest per contact.

## Versions and broken files

- `PRAGMA user_version` is the schema version. At start, each missing migration runs in its own transaction, in order;
  a migration that fails rolls back and leaves the previous version usable.
- First start with the database: the current files (`/contacts.cp`, the message rings, `/channels.cp`) are imported,
  then renamed `*.imported` rather than deleted.
- A file that does not open, or fails `PRAGMA quick_check`: it is renamed `cardputer.db.broken`, never deleted, and the
  user chooses between starting empty and restoring the SD backup. Nothing is lost silently.

## To measure before adopting it (the risks)

1. **RAM**: no PSRAM. The page cache (32 KB) plus SQLite's working memory must stay within the heap left by the radio,
   the screen buffer (65 KB) and the UI, with no allocation above 32 KB (project rule). Measure the lowest free heap
   while receiving a burst of packets with the database writing.
2. **Flash size**: SQLite adds about 500 KB of code: the dual image's app0 slot (2 MB, 958 KB used) still fits.
3. **Time**: a message from a contact not in RAM is decrypted only after its record is read and loaded (MESSAGING-12).
   Measure that read, and a transaction of 10 packets, while the radio keeps receiving.
4. **Wear**: the journal writes each changed page twice. Compare the bytes written per hour with today's files.
5. **Tests**: the simulator and the wire bench would open the same schema with the PC's SQLite, so every layer keeps
   its tests.

A proof of concept answers 1 to 4 on the Cardputer before anything else is moved.

## The trial (2026-11-08)

Built: `app/device/DbProbe` runs the schema with realistic data and logs each step (success or failure, time, free
heap, largest free block, SQLite's own memory). It runs on the PC (`make -C app/sim dbtest`, with the SQLite sources and
the settings of the device library) and on the Cardputer (console command `db`, image built from the env
`cardputer_adv_dual_dbtest`, same partitions as the dual image, test file `/littlefs/dbtest.db` deleted at the end).

Library: siara-cc/Sqlite3Esp32 2.5, SQLite 3.43.2. What its build settings mean here:
- `YYSTACKDEPTH 20` (SQLite: 100). A CREATE VIEW with a join overflows the parser ("parser stack overflow"): the
  schema has no views, the screens run their queries. Joins in queries do pass. A firmware of our own would build
  SQLite with its own settings (the library's sqlite3.c, our config).
- Its file layer syncs (fflush + fsync) but ignores an fsync error.
- Memory statistics are off by default: the trial turns them on (SQLITE_CONFIG_MEMSTATUS).

Measured on the PC with the device settings (1,000 contacts with a position, 1,000 messages, 10 rows per transaction):

| Page | Cache | SQLite memory, peak | File |
|---|---|---|---|
| 4 KB | 32 KB | 124 KB | 460 KB |
| 4 KB | 16 KB | 114 KB | 460 KB |
| 1 KB | 16 KB | 89 KB | 393 KB |
| 1 KB | 8 KB | 82 KB | 393 KB |

The trial uses 1 KB pages and a 16 KB cache by default. Firmware cost: +421 KB of code (dual app0 slot from 45 % to
66 %), +6.4 KB of static RAM.

### First run on the Cardputer (2026-11-08)

| | PC, same settings | Cardputer |
|---|---|---|
| Schema (22 statements) | 2 ms | 14 to 21 s |
| Transaction of 10 contacts | ~1 ms | 9.6 s on average, 47.9 s at worst |
| Indexed read | < 1 ms | 11 to 63 ms |
| SQLite memory, peak | 89 KB | 107 to 109 KB |
| Lowest free heap of the system | - | 56.7 KB (181 KB before) |

- The PRAGMAs that return a value gave nothing on the device (journal mode, page size, quick_check, page count): the
  settings were probably not applied, so SQLite ran with the library's (512 B pages, tiny cache, DELETE journal,
  synchronous FULL). The trial did not log why.
- The lowest heap sampled by the trial (125 KB) was wrong: the system's own low-water mark fell to 56.7 KB.
- The trial ran in loop(): the radio was served only between transactions, up to 48 s apart.
- The first run stopped on "out of memory" at its 5th transaction.

### Trial 2: diagnostics (built 2026-11-09)

Every setting is written then read back; every missing value logs its SQLite codes; the build options of the library
are listed (`build: ...`); `DbVfsMeter` counts per step the file opens, deletes, reads, writes, syncs and truncations
with their time; RESULT gives the system's lowest free heap before and after. By default the trial runs in its own
task, so loop() keeps serving the radio; `where=sd` puts the database on the SD card (each file operation takes the
card's bus lock; runs in loop() since the radio does not take that lock).

On the PC the meter already shows the cost of a random key: 300 contacts write 1.7 MB in 2,812 writes (about 5.6 KB per
contact: the pages of the unique key's index are scattered) and read 1.6 MB, the 16 KB cache being too small for the
indexes. `journal=memory sync=off` writes 1.1 MB and syncs nothing; batches of 50 sync 30 times instead of 120.

Commands for the device: `db run 100 100`, `db run 100 100 journal=memory sync=off`, `db run 100 100 where=sd`,
`db run 300 300 batch=50`.

### Trial 2 on the device (2026-11-09): the cause of the first results

Every run stopped at `PRAGMA page_size`: it ran without error (step 101, SQLITE_DONE) and returned no row. The build
options listed by the device are those of the PC build. Cause, in the library: the ESP32 VFS answers SQLITE_OK to every
file control request (`esp32.cpp`, `ESP32FileControl` returns `SQLITE_OK`). SQLite first offers each PRAGMA to the VFS
(SQLITE_FCNTL_PRAGMA, `sqlite3.c` line 136864) and, on SQLITE_OK, takes it as handled by the VFS and does nothing else.
So every PRAGMA has been ignored on the Cardputer since the first trial: 512 B pages, the library's tiny cache, DELETE
journal, synchronous FULL. The first trial's figures measure that configuration, not the intended one.

Repair, without touching the library: `DbVfsMeter` asks each newly opened file about an opcode no VFS knows; a VFS
answering SQLITE_OK to it answers everything, and the meter then returns SQLITE_NOTFOUND in its place, as SQLite's VFS
contract requires. `make -C app/sim dbtest` reproduces the fault with a PC VFS answering SQLITE_OK to everything: without
the repair the trial fails at `PRAGMA page_size` exactly as on the device, with it the whole trial passes. Any later use
of this library would need the same wrapper, or SQLite built with our own VFS.

### Trial 2 on the device with the repair (2026-11-10): settings applied, LittleFS is the bottleneck

All settings read back as asked. 100 contacts and 100 messages, 10 rows per transaction, internal flash (LittleFS):

| | journal truncate, sync normal | journal memory, sync off |
|---|---|---|
| Transaction of 10 rows | 2.8 s on average, 6.4 s at worst | 3.2 s on average, 8.6 s at worst |
| One SQLite write (about 1 KB) | 22 ms (schema) to 113 ms (messages) | 112 ms to 470 ms |
| One SQLite read (1 KB) | 43 to 124 ms | about 77 ms |
| Syncs | 37 to 147 ms each | none |
| Indexed query, average / worst | 0.6 to 2.7 ms / 10 to 20 ms | same |
| SQLite memory, peak | 87 KB (quick_check), 57 KB while inserting | 79 KB, 64 KB |
| Lowest free heap of the system | 55 KB (160 KB before) | 55 KB |
| Longest loop gap meanwhile | 117 to 153 ms (30 to 45 ms at rest) | same |

- Even without journal and without sync, a transaction takes seconds: the time goes into writes and reads of single
  pages in the middle of the file, and each costs more as the file grows. LittleFS stores files as linked blocks made
  for appending: a write in the middle of a file rewrites from there to the end, and random reads walk the chain.
  SQLite's work is exactly that (B-tree pages scattered in one file).
- Memory: about 125 KB taken at the worst moment (SQLite's peak, the trial task's 16 KB stack, LittleFS buffers),
  leaving 55 KB to the firmware.

Against the thresholds below: transactions about 15 times too slow, memory floor missed by 45 KB, indexed reads within
the limit on average. **Verdict: no SQLite on the internal flash.** The SD card (FAT, writes in place) remains to be
measured (`db run 100 100 where=sd`) before ruling SQLite out for the message history; the memory floor would have to
be met there too.

Lesson for the alternative (MESSAGING-12): any format kept on LittleFS must avoid rewriting the middle of a big file.
Prefer appending (a log compacted from time to time) or many small files (records grouped by the first byte of the key),
and keep in-place updates to small files, as the message rings already do (about 15 KB each).

### SD card (2026-11-10): fast, but a write failed

`db run 100 100 where=sd` (FAT, journal truncate, sync normal): the schema took 1.6 s (282 writes in 504 ms, about 1.8 ms
each; 42 syncs in 468 ms), the first transaction 129 ms, transactions 137 ms on average and 202 ms at worst: within the
200 ms threshold, about 20 times faster than LittleFS. The trial then stopped at its 8th transaction on
`disk I/O error (code 778)`, SQLITE_IOERR_WRITE: an `fseek` or `fwrite` of the ESP32 VFS failed (`ESP32DirectWrite`),
with nothing said about which file, where, or why.

Found in the library while looking for it: its `ESP32Truncate` does nothing (`ftruncate` is under `#if 0`) and answers
SQLITE_OK. With journal_mode=truncate a commit truncates the journal; here the journal stays valid on the card, and a later
open would take it as a hot journal and roll the database back. journal_mode=persist (clears the header by a write) and
delete (removes the file) do not depend on it.

The trial now names the first failed file operation (operation, file, offset, size, SQLite code, errno), reports a VFS
that ignores truncations, and only judges memory when the system's lowest free heap went down during the trial (it
counts since boot: the 55 KB of this run came from the earlier trials).

### SD card, three journal modes (2026-11-10, after a restart)

100 contacts and 100 messages, 10 rows per transaction, page 1 KB, cache 16 KB, run in loop():

| | persist | delete | truncate |
|---|---|---|---|
| Result | ok | ok | ok, ignored truncations reported |
| Transaction of 10 rows, average / worst | 140 / 511 ms | 147 / 474 ms | 130 / 464 ms |
| One write / one sync | about 1.6 ms / 12 ms | about 1.2 ms / 12 ms | about 1.6 ms / 8 ms |
| Indexed query, average / worst | 0.5 to 1.1 ms / 3 to 8 ms | same | same |
| SQLite memory, peak | 87 KB (quick_check), 57 KB while inserting | same | same |
| Lowest free heap of the system | 80 KB (176 KB before: 96 KB taken) | not measured (reached before) | not measured |

- The write error of the first SD run did not come back in these three runs. Its cause is unknown: an occasional SD
  write failure cannot be ruled out without a longer run.
- Speed meets the threshold on average; the worst transactions (about 0.5 s) do not.
- Memory misses the floor by 20 KB. The peak comes from quick_check (87 KB); while inserting SQLite peaks at 57 KB.
- In loop(), the radio waited up to 1.6 s (the schema) and about 140 ms per transaction.

**Verdict for the SD card: possible for the message history, not proven.** Conditions: the two repairs of the library's
VFS (file control, truncate: use persist or delete), every SQLite call in the radio's task (the radio does not take the
card's bus lock), a lower memory peak (cache 8 KB, quick_check only at start-up), a long run without a write error, an SD
card always present, and 430 KB of flash. Without a card, contacts and messages need another store anyway.

### SD card, 1000 contacts and 1000 messages, cache 8 KB (2026-11-10, after a restart): rejected

`db run 1000 1000 where=sd journal=persist cache=8`, page 1 KB, 10 rows per transaction, run in loop():

| Step | Time | File system |
|---|---|---|
| schema (22 statements) | 1.8 s | 68 syncs, 705 ms |
| 1000 contacts + positions (100 transactions) | 103.6 s, about 1 s each | read 15.2 MB (54 s), wrote 10.7 MB (29 s), 1231 syncs (15 s) |
| 1000 messages (100 transactions) | 28.5 s, about 285 ms each | read 4.5 MB (14 s), wrote 2.4 MB (6 s) |
| indexed lookups (key prefix, name, position, conversations) | 0.5 to 0.7 ms average, worst 2.6 to 7 ms | |
| last 10 messages of a conversation | 10.5 ms average, 15 ms worst | 5 pages read per query |
| quick_check (file 393 KB) | 1.1 s | |
| close / reopen / count | 3 / 67 / 30 ms | |

RESULT: **failed at memory**. Lowest free heap of the system 176 KB before, **86 KB** after; SQLite's peak 81 KB
(quick_check), 58 KB while inserting; transactions 658 ms on average, 1366 ms at worst.

- No file error in about 22,000 writes and 21,000 reads: the write error of the first SD run did not come back.
- Cache 8 KB instead of 16 KB saved nothing at the peak (58 KB while inserting, as before): the memory goes to the
  parsed schema, the prepared statements and the lookaside, not to the page cache. The small cache costs time instead:
  every contact transaction reads about 150 pages, because a 32-byte public key and a name land at random places in
  three indexes.
- Transactions get slower as the file grows (140 ms at 100 contacts, about 1 s at 1000). MESSAGING-12 targets more than
  1000 contacts.
- Outside SQLite, the system lost about 14 KB more (181 - 81 = 100 KB expected, 86 KB measured): card and file buffers.

**Decision: no SQLite in the firmware, neither on the internal flash nor on the SD card.** With 1000 contacts it misses
two of the three thresholds (memory, 86 KB instead of 100; speed, 658 ms instead of 200), it needs two repairs of the
library, every call must run in the radio's task, and it needs a card. MESSAGING-12 goes back to "contacts in RAM +
archive in files": append-only records or small files per key prefix, written rarely (LittleFS rewrites the middle of a
big file slowly), on the SD card when there is one and on the internal flash otherwise. The schema (`schema.sql`) stays
as the reference of what to store. The trial code (`DbProbe`, `DbVfsMeter`, env `cardputer_adv_dual_dbtest`,
`make -C app/sim dbtest`) can be removed, or kept for a later SQLite version or a board with PSRAM.

### Decision thresholds (set before trial 2)

SQLite stays a candidate if, with the best settings, on the internal flash:
- a transaction of 10 rows takes less than 200 ms, and an indexed read less than 5 ms;
- the system's lowest free heap stays above 100 KB;
- no more packets are lost during the trial than at rest.

Otherwise: no SQLite on LittleFS. Back to "contacts in RAM + fixed-record archive" (MESSAGING-12); SQLite on the SD card
may remain for the message history only.
