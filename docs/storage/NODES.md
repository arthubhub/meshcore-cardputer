# Every node heard, and the route of every packet (design, 2026-11-10)

Design for a list of nodes far bigger than MeshCore's contact table, and for tracing the route a packet took. Not built
yet. It replaces the SQLite proposal (`README.md`, rejected after the trials on the Cardputer), keeps `schema.sql` as the
reference of what to store, and covers MESSAGING-12, MESSAGING-14, MAP-9 and part of MAP-8 in the roadmap.

## The problem, measured

- **The contact table is RAM.** MeshCore's `ContactInfo` takes 184 bytes (key 32, name 32, route 64, shared secret 32,
  the rest 24). 100 contacts = 18.4 KB static; 1000 would take 184 KB, all the free heap. The table cannot grow much.
- **Repeaters fill it.** On a busy mesh most adverts come from repeaters, and every advert heard becomes a contact. A
  repeater only needs to be a contact to log in to it; to name it in a route, a much smaller record is enough.
- **Without the contact, a message is lost.** A direct message names its sender by one byte of their key; MeshCore
  tries the contacts with that byte (`Mesh::onRecvPacket` → `searchPeersByHash` → `getPeerSharedSecret`). Not in the
  table = not decrypted (VALIDATION-11 made the table replace an old contact instead of refusing new ones, but the
  replaced one is forgotten).
- **A bigger list makes routes more ambiguous, not less.** A route holds 1, 2 or 3 bytes of each repeater's key. With
  N repeaters known, the chance that another one shares the ID of a given hop:

  | Repeaters known | 1-byte IDs | 2-byte IDs | 3-byte IDs |
  |---|---|---|---|
  | 100 | 32 % | 0.2 % | 0 |
  | 200 | 54 % | 0.3 % | 0 |
  | 500 | 86 % | 0.8 % | 0 |
  | 1000 | 98 % | 1.5 % | 0 |

  With 1-byte IDs (MeshCore's default, still what most nodes send), knowing every repeater of a region makes almost
  every hop ambiguous. Tracing a route therefore needs more than the list: where the nodes are, and which nodes have
  already been seen next to each other.

## Objectives

| # | Objective | Measure of success |
|---|---|---|
| O1 | Never lose a message from someone we have heard, whatever the size of the list | a direct message from a node absent from the contact table but known to the archive is decrypted, shown and acknowledged (checked with MeshCore's real code in `app/sim/wire`) |
| O2 | Know every node heard: people, repeaters, rooms, sensors | at least 2,500 nodes on the internal flash with key, name, type, position, first and last heard, hops, signal |
| O3 | Trace the route of any packet heard (messages, adverts, discovery answers), with how sure each hop is | on a simulated mesh of 500 repeaters with 1-byte IDs, more hops placed right than the current resolver |
| O4 | Search all of them by name (MESSAGING-14, the @ suggestions of MESSAGING-13) | results while typing, under 300 ms for 2,500 nodes |
| O5 | Fit the device | + 8 KB RAM at most; a fixed flash budget known in advance; no operation in `loop()` above 50 ms; a power cut loses at most the last minute of "last heard" times, never the archive; no SD card needed |
| O6 | Keep the 100 contact places for people | repeaters, rooms and sensors stay out of the contact table unless they are favourites or the user adds them |

Not aimed at: SQL queries, the history of every position (later, MAP-1), syncing with the phone app (SYNC-1, but the
archive gives it a place to merge into).

## The design: two tiers

```
                 advert / packet heard                       message to me
                         │                                         │
                         ▼                                         ▼
   ┌───────────── CardputerMesh (BaseChatMesh) ─────────────────────────────────┐
   │  contact table, RAM, 100 x 184 B: people we talk to, favourites            │
   │     ▲ promote (message decrypted, user adds)   │ demote (place needed)     │
   └─────┼──────────────────────────────────────────┼───────────────────────────┘
         │                                          ▼
   ┌─────┴──────── NodeArchive (portable, app/core) ─────────────────────────────┐
   │  every node heard: 96 B records in buckets by the first byte of the key     │
   │  write-behind queue (RAM)  ·  hash cache (RAM)  ·  LinkBook (who is next to │
   │  whom, from every route heard)                                              │
   └─────────────────────────────┬───────────────────────────────────────────────┘
                                 ▼
          Storage (LittleFS through cpadv::DataFs; the same code runs on a fake on the PC)
```

### Tier 1: the contact table (unchanged size)

MeshCore's own table, 100 places, full `ContactInfo`. Holds who we talk to: people and favourites, plus repeaters and
rooms the user adds. Repeaters, rooms and sensors heard by advert go to the archive only
(`shouldAutoAddContactType()` returns false for them; MeshCore then still calls `onDiscoveredContact()`, which feeds the
archive). The ones already in the table at the update stay; a setting can bring back the old behaviour.

### Tier 2: the archive (NodeArchive)

**Record, 96 bytes, versioned:** public key 32, name 32, type, flags (favourite, was a contact, imported, has a
conversation), hops of the last advert, SNR and RSSI of the last hop, latitude and longitude, position time (sender's
clock), first heard and last heard (our clock), last advert timestamp (their clock: the replay check when the node comes
back into the table), CRC.

**Buckets, not one big file.** The trials showed that LittleFS rewrites the middle of a big file very slowly (the CTZ
list is copied from that point to the end). So records live in 128 small files, `/nodes/bNN.bin`, one per value of
`key[0] >> 1`. A file is a header (version, count, CRC) and its records, at most 42 = one 4 KB flash block.

- The 1 to 3 byte ID of a hop, and the 1 byte that names the sender of a direct message, are the start of the key: each
  lookup reads **one** file of a few hundred bytes. No index in RAM.
- An update rewrites one whole small file. LittleFS writes files copy-on-write and switches on close, so a power cut
  leaves the old version or the new one, never a mix (the PC fake must reproduce this).
- Capacity: 128 x 42 = 5,376 nodes at most; the flash budget follows the files really written (about 4 KB per bucket
  in use, 512 KB when all are full). `NodeArchive::setCapacity`, like `MessageStore::setCapacity`, takes a lower record
  limit per bucket on a small partition, so messages keep their share.
- A full bucket drops its weakest record: never a contact, a favourite or a node with a conversation; first the one
  heard the longest time ago, ties broken by the most hops. The log names what leaves.
- A damaged bucket (bad CRC, unknown version) is set aside (`bNN.bad`) and logged with its size and header; the other
  buckets are untouched.

**Write-behind.** Adverts arrive in bursts and repeat. Changes wait in a RAM queue (24 records, 2.3 KB), merged by key,
and are written one bucket per `loop()` pass when the queue is full, when the oldest change is 60 s old, before the
screen goes off, before a restart and on low battery (POWER-2). A record is only rewritten when something that matters
changed (name, type, position, flags), or when its "last heard" is more than an hour old: the count of flash writes
stays low (estimated wear: under 2 erases per block per day on a busy mesh, decades of life).

**Hash cache.** The packet log resolves a hop for every packet heard: a 64-entry RAM cache (ID → the chosen node, 1.5
KB) avoids reading the same bucket again.

### Archive rules, record version 1 (built, step 2)

- **Record, 96 bytes, little endian, field by field:** key 32, name 32, type, flags, hops (0xFF unknown), SNR x4,
  RSSI (dBm, clamped to -128..127), position source, 2 reserved, latitude, longitude, position time, first heard, last
  heard, last advert time. `NodeRecordCodec` is the only code that knows these bytes.
- **Flags:** Favourite, InContacts, Keep (has a conversation or was a contact), Imported (never heard), HasPos. The
  first three protect a record from being dropped; the mesh layer sets them (`setFlags`), never `upsert`.
- **Fixed geometry, no capacity setting:** 128 buckets x 42 records. A LittleFS file takes a 4 KB block however small
  it is (tiny inline files aside), so fewer records per bucket would save no flash; about 516 KB when every bucket is in
  use, to be measured on the device (step 3).
- **Merge of a node heard again:** a non-empty name and the type replace the stored ones; last heard and last advert
  time only go forward; first heard keeps the oldest; hops and signal come from the newest hearing only; the position
  follows `ContactPosition::offer` (newest by the sender's clock, an unknown time never replaces a known one). An
  import never replaces what was heard, it only fills a missing name or position; a node heard loses Imported.
- **Wear rule:** heard again with nothing new but its time and signal, and its stored "last heard" less than an hour
  old: nothing is written. 20 adverts in an hour cost one write.
- **Full bucket:** the weakest unprotected record leaves: imported ones first, then the oldest heard, ties broken by
  the most hops. A newcomer that is not stronger than that one (an import) is refused; a bucket of protected records
  refuses with a warning. Every departure is logged with both names.

### Promotion and demotion

- **A message from a node outside the table (O1).** `CardputerMesh` overrides three virtual methods of MeshCore,
  without changing `meshcore/`:
  1. `searchPeersByHash(hash)`: the table's matches first (the base class), then up to 8 archive records with that
     byte (types that can send to us), kept in a small array of candidates;
  2. `getPeerSharedSecret(dest, j)`: for an archive candidate, computes the shared secret from its key (ECDH, on
     demand; secrets are never stored in the archive: they would let anyone read the conversation);
  3. `onPeerDataRecv` / `onPeerPathRecv(sender_idx)`: when the winner is an archive candidate, puts it in the table
     (demoting one), runs the base class's search again to find its new index, and hands the packet to the base class
     with that index. The message, its ACK and its route then follow MeshCore's normal path.
  The candidate count bounds the cost of a forged packet (8 ECDH at most per packet; measured on the device first).
- **The user adds a node** from the archive list: same promotion.
- **Demotion** (a place is needed, VALIDATION-11's rule): the contact is already in the archive, marked "was a contact";
  its conversation stays and still finds the name. Its learnt route is lost: MeshCore floods and learns it again.

### Tracing a route (O3)

Three sources of evidence, combined by `RouteResolver`:

1. **Who can be a hop:** archive records of relaying types (repeaters, `NodeLookup::Kind::Relay`), heard recently.
2. **Who has been seen next to whom: LinkBook.** Every flood heard carries its whole route: an advert from R through
   x then y gives the links R–x, x–y and y–me (the last hop is a neighbour: its SNR is known, and RADIO-8's discovery
   lists them). LinkBook keeps the links seen, by ID, with a count and a date: 256 links of 8 bytes = 2 KB of RAM,
   saved in one file. A link seen many times between two IDs is strong evidence for the pair.
3. **Where they are:** positions from the adverts. Two consecutive hops more than about 80 km apart are not possible
   (to tune from real routes).

The resolver today picks each ambiguous hop alone, by distance to its neighbours. It becomes a **best-path search over
the whole route** (Viterbi): at most 64 hops x 8 candidates, a few thousand operations. The cost of a pair adds the
distance, a penalty when LinkBook has never seen the link, a penalty for an old record and a ban above the maximum
range; the two ends are anchored (the sender's position when known; the last hop must be a node we hear). Each hop
keeps its state (certain, chosen among N, unknown) and the screens show it as today.

What else removes ambiguity:
- **2-byte IDs.** RADIO-10 already lets our floods use them; every node that sends 2-byte IDs makes its routes almost
  certain (1.5 % ambiguity at 1,000 repeaters). Worth explaining in the README and in the region guide.
- **A trace** (MESSAGING-5) gives the exact route and the SNR of each hop on demand.

### Growing the list faster

- Passive, the main source: every advert heard goes to the archive, whatever its type. A repeater floods its advert
  every 47 hours by default (`simple_repeater`, `flood_advert_interval`; owners can change it), so it takes about two
  days of listening to collect a region, and the archive must survive restarts to get there. This is also why
  records must not be dropped quickly: a repeater silent for a day is normal.
- Import from the SD card: a file of nodes (key, name, type, position) made on a PC from the phone app's export or a
  public node map, marked "imported, never heard" and ranked below heard nodes. Formats and licences to check first.
- The phone mode's contacts (SYNC-1) merge into the archive by key.

### Search (O4)

A scan of every bucket (5,376 records of 96 bytes at most: 516 KB of sequential reads), a few buckets per `loop()`
pass so the radio keeps being served, results added as they come. Names are folded (case, accents) at write time.

## Measures on the device (step 3, 2026-11-18)

Console benches of the standalone firmware (`app/device/DeviceBench`, portable part `app/core/drivers/StoreBench`).

**LittleFS operations** (`bench fsops`, 4 KB files, 240 MHz): every operation costs a directory lookup (9 ms with 16
files in the directory, 16 ms with 160) and directory operations get slower with every entry; outliers of 0.7 to 4 s
happen when LittleFS compacts a directory. Through Arduino's `File`, opening a file makes several lookups:

| 4 KB file, 240 MHz | Arduino `File` | POSIX `open/read/write` |
|---|---|---|
| read the whole file | 55 to 59 ms | **6 ms** |
| replace the whole file | 74 to 122 ms (worst 0.7 s) | 41 to 117 ms (worst 0.7 s) |
| rename / remove | 45 to 200 ms / 30 to 75 ms | |

**The archive's buckets** (`bench store`, 128 buckets, 42 records):

| | first design (root, temporary file + rename, `File`) | 8 directories, one atomic write, POSIX, 240 MHz | same, 80 MHz |
|---|---|---|---|
| rewrite a full bucket, avg / p95 | 0.6 to 6 s, growing | 58 / 124 ms | 118 / 336 ms |
| read a bucket, avg / p95 | | 19.5 / 25 ms | 54 / 67 ms |
| every bucket once | | 2.9 s | 8.6 s |
| longest pause of `loop()`, storage in `loop()` | 6.5 s | 395 ms | 842 ms |
| longest pause of `loop()`, storage in its own task | | **53 ms** | **99 ms** |

- Flash: 6 KB per full bucket (data block + directory metadata), **about 770 KB for a full archive**, 8 KB per directory
  even empty.
- **ECDH** (`bench ecdh`, MeshCore's `calcSharedSecret`): **14.2 ms at 240 MHz, 43.2 ms at 80 MHz**, steady.

**Decisions:**
1. `LittleFsStorage` uses POSIX calls for everything (also used by the messages and the contacts: the contacts load in
   0.6 s instead of 3.2 s at boot).
2. A bucket file is replaced by one write when the storage is atomic (LittleFS); the temporary file and rename stay for
   storage that is not (FAT, the PC fake). Buckets live in 8 subdirectories of 16 (`/nodes/0` .. `/nodes/7`).
3. The archive's flash work runs in a task of its own, at a low priority: `loop()` then pauses 53 to 99 ms at worst
   (the flash's own erase and program time, when both cores stop) instead of up to 0.8 s.
4. Two locks: the RAM queue's (held for microseconds: `loop()` never waits for a write) and the archive's (held during
   a read or write; a lookup from `loop()` may wait for one bucket write, about 0.1 to 0.3 s, and lookups are rare).
5. An unknown sender costs at most 8 ECDH (114 ms) with the screen on, 4 (173 ms) with it off, only for node types
   that can send that kind of packet, and at most 32 ECDH per minute against forged packets.
6. Search and the node count read every bucket: 2.7 to 8.6 s, so they run in the background and show results as they
   come; no full read at boot.

## What was rejected and why

| Option | Why not |
|---|---|
| Raise `MAX_CONTACTS` | 184 B per place, static; +100 places = +18.4 KB and still a hard limit. A small raise (to 150) stays possible after measuring the heap. |
| SQLite | measured: 86 KB of free heap at worst, 0.7 to 1 s per transaction at 1,000 contacts, two library bugs to repair, SD only (`README.md`) |
| One append-only log with an index in RAM | 6 to 8 B of RAM per node (24 KB for 3,000), and compaction rewrites the whole file, slow on LittleFS |
| The SD card as the main store | optional hardware, a bus shared with the radio which does not take the card's lock; kept for import, export and backup |
| Store the shared secrets to skip the ECDH | a stored secret lets anyone holding the file read the conversation |

## Unknowns to measure before building on them

1. Time to read and to rewrite a 4 KB bucket on the device's LittleFS (expected under 5 ms and 30 to 100 ms), with
   and without the radio receiving.
2. Time of one `calcSharedSecret` on the ESP32-S3: sets the number of candidates.
3. Flash taken by 128 small files (LittleFS metadata, inline files).
4. On the PC, with MeshCore's real code: a direct message from an archived node is decrypted, acknowledged and its
   sender promoted; a forged packet costs at most 8 ECDH.
5. On a simulated mesh (500 repeaters on a map, 1-byte IDs, routes drawn from real radio ranges): hops placed right by
   the current resolver, by the best-path search, and by the best-path search with LinkBook.

## Building it, bottom up

| Step | What | Checked by |
|---|---|---|
| 1 | `BucketStore`: fixed-size records in bucket files over `Storage` (atomic rewrite, CRC, set-aside of a bad bucket) and a PC fake that can cut the power in the middle of a write | unit tests. **Built 2026-11-10**: `app/core/drivers/BucketStore`, `app/sim/fakes/FakeStorage`, 11 tests (a cut at every step of a save, with and without an older file) |
| 2 | `NodeRecord` codec and `NodeArchive` (upsert, find by ID, weakest-record eviction) | unit tests. **Built 2026-11-10**: `app/core/drivers/NodeRecord`, `NodeArchive`, 10 tests (rules below) |
| 3 | Device bench (`bench store`, `bench fsops`, `bench ecdh` in the console): unknowns 1 to 3 | **Measured 2026-11-18** on the Cardputer: see "Measures on the device" |
| 4 | Write-behind queue and hash cache | **Built 2026-11-18**: `NodeBook` (queue of hearings, flag changes and removals, merged per node, written a bucket at a time outside the queue's lock), `NodeMatchCache` (32 answers, 30 s) |
| 5 | Mesh integration: every advert to the archive, repeaters out of the table, promotion on decrypt, demotion | **Built 2026-11-18**: `ArchivingChatMesh` (wire tests with MeshCore's real code: the reported bug reproduced, then fixed), `DeviceNodeArchive` (storage task on core 0). On the device: 1,000 made-up nodes, 0 dropped at 2 per second, `loop()` pauses under 175 ms |
| 6 | LinkBook and the best-path search in `RouteResolver` | the simulated mesh (unknown 5), unit tests |
| 7 | Screens: "Nodes heard" list with search, node sheet, add to contacts, routes of the packet log on the map (MAP-9) | scenarios with screenshots in both languages |
| 8 | Import from the SD card, SYNC-1 merge | unit tests, scenario |
| 9 | Hardware | `app/TESTS.md` checklist |

Steps 1 to 4 change nothing the user sees, and each can stop the plan if a measure says no.
