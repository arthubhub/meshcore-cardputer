-- Keyboard firmware database, schema version 1 (proposal, 2026-11-08). Not used by the firmware yet.
--
-- One SQLite file on the internal data partition (/littlefs/cardputer.db) for everything the keyboard firmware learns:
-- contacts and their positions, channels, conversations, messages with their route, packets heard, discovery
-- results, GPS tracks. The phone mode keeps its own files (SYNC-1 would map them to these tables).
--
-- Conventions
--   *_at     our clock, unix seconds (the Cardputer's RTC, set by GPS or adverts)
--   *_time   the sender's clock, unix seconds (from inside a packet); comparable between packets of one sender only
--   lat_e6   degrees * 1e6, as MeshCore stores them
--   path     raw MeshCore path bytes, with path_wire = the wire byte (bits 6-7: ID size - 1, bits 0-5: hop count)
--   pub_key  32 bytes. A path or a packet only carries 1 to 3 bytes of it: find a node from those bytes with a range on
--            the primary key, which uses the index:  pub_key >= x'a1' AND pub_key < x'a2'
--
-- Device settings (see docs/storage/README.md): page_size 4096, cache_size -32 (32 KB), journal_mode TRUNCATE (no WAL:
-- it needs shared memory), synchronous NORMAL, foreign_keys ON, temp_store MEMORY. Writes are grouped in one
-- transaction every few seconds, or every 10 packets.

PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------- schema bookkeeping
-- PRAGMA user_version holds the schema version; migrations run in order at start (README.md).
CREATE TABLE meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
) WITHOUT ROWID;
-- rows: 'created_at', 'created_by' (firmware version), 'migrated_from' ...

-- ---------------------------------------------------------------- nodes
-- Every node ever heard, without limit. Only `in_ram` ones are loaded into MeshCore's table (100 places): the rest
-- comes back when it advertises or writes to us (MESSAGING-12).
CREATE TABLE contacts (
  id              INTEGER PRIMARY KEY,
  pub_key         BLOB    NOT NULL UNIQUE CHECK (length(pub_key) = 32),
  name            TEXT    NOT NULL,
  name_fold       TEXT    NOT NULL,              -- lower case, accents removed: search (MESSAGING-14, @ suggestions).
                                                 -- Start of a name: name_fold GLOB 'elo*' uses the index (LIKE does not);
                                                 -- anywhere in it: instr(name_fold, 'elo') > 0 reads the whole table
  type            INTEGER NOT NULL,              -- 1 person, 2 repeater, 3 room, 4 sensor (MeshCore ADV_TYPE_*)
  favourite       INTEGER NOT NULL DEFAULT 0,
  flags           INTEGER NOT NULL DEFAULT 0,    -- other MeshCore contact flags (telemetry permissions...)
  in_ram          INTEGER NOT NULL DEFAULT 0,    -- loaded in MeshCore's table right now
  first_heard_at  INTEGER NOT NULL,
  last_heard_at   INTEGER NOT NULL,              -- MeshCore's lastmod: picks who leaves RAM first
  last_advert_time INTEGER NOT NULL DEFAULT 0,   -- MeshCore's replay protection
  out_path_wire   INTEGER,                       -- NULL: no route known, messages flood
  out_path        BLOB,
  position_id     INTEGER REFERENCES positions(id) ON DELETE SET NULL,   -- the newest position, NULL if none
  advert_raw      BLOB                           -- the last signed advert, to share the contact (MeshCore putBlobByKey)
);
CREATE INDEX contacts_by_name     ON contacts(name_fold);
CREATE INDEX contacts_by_activity ON contacts(in_ram, favourite, last_heard_at);

-- Where a contact was, each time we learnt it. The newest by `time` wins (ContactPosition::offer): a late advert never
-- replaces a newer message. The history feeds maps and range tests later.
CREATE TABLE positions (
  id           INTEGER PRIMARY KEY,
  contact_id   INTEGER NOT NULL REFERENCES contacts(id) ON DELETE CASCADE,
  lat_e6       INTEGER NOT NULL,
  lon_e6       INTEGER NOT NULL,
  time         INTEGER NOT NULL,              -- when they were there, by their clock (0 unknown)
  received_at  INTEGER NOT NULL,
  source       INTEGER NOT NULL,              -- 1 advert, 2 message, 3 telemetry
  message_id   INTEGER REFERENCES messages(id) ON DELETE SET NULL,
  packet_id    INTEGER REFERENCES packets(id) ON DELETE SET NULL
);
CREATE INDEX positions_by_contact ON positions(contact_id, time);

-- ---------------------------------------------------------------- channels and conversations
CREATE TABLE channels (
  id         INTEGER PRIMARY KEY,
  name       TEXT    NOT NULL UNIQUE,         -- "Public", "#toulouse"
  secret     BLOB    NOT NULL CHECK (length(secret) IN (16, 32)),
  hash       INTEGER NOT NULL,                -- first byte of SHA-256(secret): what packets carry
  joined_at  INTEGER NOT NULL,
  position   INTEGER NOT NULL DEFAULT 0       -- order in MeshCore's channel table
);

-- A direct conversation (contact_id) or a channel (channel_id), never both.
CREATE TABLE conversations (
  id             INTEGER PRIMARY KEY,
  contact_id     INTEGER UNIQUE REFERENCES contacts(id) ON DELETE SET NULL,
  channel_id     INTEGER UNIQUE REFERENCES channels(id) ON DELETE CASCADE,
  name           TEXT    NOT NULL,            -- kept when the contact is gone
  unread         INTEGER NOT NULL DEFAULT 0,
  last_at        INTEGER NOT NULL DEFAULT 0,
  last_preview   TEXT    NOT NULL DEFAULT '',
  last_outgoing  INTEGER NOT NULL DEFAULT 0,
  muted          INTEGER NOT NULL DEFAULT 0,
  CHECK ((contact_id IS NULL) OR (channel_id IS NULL))
);
CREATE INDEX conversations_by_activity ON conversations(last_at);

-- ---------------------------------------------------------------- messages
CREATE TABLE messages (
  id               INTEGER PRIMARY KEY,
  conversation_id  INTEGER NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
  outgoing         INTEGER NOT NULL,
  sender_id        INTEGER REFERENCES contacts(id) ON DELETE SET NULL,   -- direct: the contact; channel: a name match
  sender_name      TEXT,                      -- channel messages only carry a name
  text             TEXT    NOT NULL,
  sent_time        INTEGER,                   -- sender's clock (MeshCore timestamp, also the dedup key)
  received_at      INTEGER NOT NULL,          -- or written, for ours
  state            INTEGER NOT NULL,          -- MsgState: received, pending, sent flood/direct, delivered, failed...
  attempts         INTEGER NOT NULL DEFAULT 0,
  ack_tag          INTEGER,                   -- expected ACK of our direct messages
  snr_x4           INTEGER,
  rssi             INTEGER,
  hops             INTEGER,                   -- NULL unknown
  route_type       INTEGER,                   -- wire::Route of the packet
  path_kind        INTEGER NOT NULL DEFAULT 0, -- 0 none, 1 taken (from the packet), 2 planned (the contact's route)
  path_wire        INTEGER,
  path             BLOB,                      -- the whole path, in travel order (MESSAGING-10, no 32-byte cut)
  packet_id        INTEGER REFERENCES packets(id) ON DELETE SET NULL
);
CREATE INDEX messages_by_conversation ON messages(conversation_id, id);
CREATE INDEX messages_pending         ON messages(state) WHERE state IN (1, 2, 3);
-- Full-text search across conversations (MESSAGING-7) can be an FTS5 table later: optional, it costs flash.

-- Who a message mentions, from "@[name]" (MESSAGING-13)
CREATE TABLE mentions (
  message_id  INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
  name        TEXT    NOT NULL,               -- as written
  contact_id  INTEGER REFERENCES contacts(id) ON DELETE SET NULL,
  is_me       INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (message_id, name)
) WITHOUT ROWID;
CREATE INDEX mentions_of_me ON mentions(is_me) WHERE is_me = 1;

-- ---------------------------------------------------------------- radio
-- Packets heard and sent (RADIO-6), with their whole path (MAP-9). Kept within a budget (README.md: last N rows).
CREATE TABLE packets (
  id              INTEGER PRIMARY KEY,
  at              INTEGER NOT NULL,           -- our clock
  uptime_ms       INTEGER NOT NULL,
  outgoing        INTEGER NOT NULL,
  payload_type    INTEGER NOT NULL,           -- wire::Payload
  route_type      INTEGER NOT NULL,
  hash            BLOB,                       -- MeshCore packet hash: duplicates heard through other repeaters
  path_wire       INTEGER NOT NULL,
  path            BLOB,
  transport_code  INTEGER,                    -- region (RADIO-10), NULL unscoped
  our_region      INTEGER,
  snr_x4          INTEGER,
  rssi            INTEGER,
  length          INTEGER NOT NULL,
  from_prefix     BLOB,                       -- 1 to 3 bytes of the sender's key (or channel hash)
  from_is_channel INTEGER NOT NULL DEFAULT 0,
  to_hash         INTEGER
);
CREATE INDEX packets_by_time ON packets(at);
CREATE INDEX packets_by_hash ON packets(hash);

-- Repeater discovery answers (RADIO-8)
CREATE TABLE discoveries (
  id          INTEGER PRIMARY KEY,
  at          INTEGER NOT NULL,
  pub_prefix  BLOB    NOT NULL,               -- 8 bytes of the repeater's key
  contact_id  INTEGER REFERENCES contacts(id) ON DELETE SET NULL,
  snr_there_x4 INTEGER,
  snr_here_x4  INTEGER,
  rssi        INTEGER
);
CREATE INDEX discoveries_by_time ON discoveries(at);

-- ---------------------------------------------------------------- GPS
-- Recorded tracks (MAP-1), range tests (RADIO-3) and signal hunts (RADIO-2) share these
CREATE TABLE tracks (
  id         INTEGER PRIMARY KEY,
  kind       INTEGER NOT NULL,                -- 1 track, 2 range test, 3 signal hunt
  name       TEXT    NOT NULL,
  started_at INTEGER NOT NULL,
  ended_at   INTEGER,
  target_id  INTEGER REFERENCES contacts(id) ON DELETE SET NULL
);
CREATE TABLE track_points (
  track_id   INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
  at         INTEGER NOT NULL,
  lat_e6     INTEGER NOT NULL,
  lon_e6     INTEGER NOT NULL,
  alt_dm     INTEGER,                         -- decimetres
  speed_cms  INTEGER,
  rssi       INTEGER,                         -- range tests / hunts: the signal at that point
  snr_x4     INTEGER,
  acked      INTEGER,                         -- range test: the ping was answered
  PRIMARY KEY (track_id, at)
) WITHOUT ROWID;

-- ---------------------------------------------------------------- queries of the screens
-- No views: the SQLite build of the ESP32 library limits its parser stack to 20 entries (YYSTACKDEPTH), and a CREATE
-- VIEW with a join overflows it ("parser stack overflow"). The screens run the queries themselves:
--   newest position of a contact:
--     SELECT p.lat_e6, p.lon_e6, p.time, p.source FROM contacts c JOIN positions p ON p.id = c.position_id WHERE c.id = ?
--   conversation list:
--     SELECT id, name, unread, last_at, last_preview, last_outgoing, channel_id IS NOT NULL FROM conversations
--     ORDER BY last_at DESC LIMIT ?
