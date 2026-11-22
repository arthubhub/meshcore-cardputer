// Unit tests of the portable drivers (core/drivers) against fake chips (sim/fakes).
// Layer 1 of the test pyramid: each hardware block alone, no UI.
//   make -C app/sim test        (CORE_LOG=1 to see the driver logs)
#include "../../core/KeyMatrix.h"
#include "../../core/Services.h"
#include "../../core/drivers/AirtimeMeter.h"
#include "../../core/drivers/Carousel.h"
#include "../../core/drivers/BucketStore.h"
#include "../../core/drivers/ContactCapacity.h"
#include "../../core/drivers/ContactPosition.h"
#include "../../core/drivers/PositionBook.h"
#include "../../core/drivers/RadioConfig.h"
#include "../../core/drivers/GeoText.h"
#include "../../core/drivers/HeadingFilter.h"
#include "../../core/drivers/FlashFsProbe.h"
#include "../../core/drivers/FloodOptions.h"
#include "../../core/drivers/GpsLink.h"
#include "../../core/drivers/MapProjection.h"
#include "../../core/drivers/Mentions.h"
#include "../../core/drivers/NotifyPolicy.h"
#include "../../core/drivers/NodeArchive.h"
#include "../../core/drivers/NodeBook.h"
#include "../../core/drivers/NodeMatchCache.h"
#include "../../core/drivers/NodeDiscovery.h"
#include "../../core/drivers/PacketLog.h"
#include "../../core/drivers/Palette.h"
#include "../../core/drivers/RouteResolver.h"
#include "../../core/drivers/StoreBench.h"
#include "../../core/drivers/Pi4ioe.h"
#include "../../core/drivers/Tca8418.h"
#include "../../core/drivers/TileGrid.h"
#include "../../core/drivers/TrafficTracer.h"
#include "../../core/drivers/TileRasterizer.h"
#include "../fakes/FakeGpsUart.h"
#include "../fakes/FakeI2c.h"
#include "../fakes/FakeStorage.h"

#include <functional>
#include <map>
#include <memory>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

extern std::string g_coreLogCapture;
extern bool g_coreLogQuiet;

static int g_checks = 0, g_failed = 0;
static const char* g_test = "";

#define CHECK(cond)                                                                 \
  do {                                                                              \
    g_checks++;                                                                     \
    if (!(cond)) {                                                                  \
      g_failed++;                                                                   \
      printf("  FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, g_test, #cond);          \
    }                                                                               \
  } while (0)
#define CHECK_EQ(a, b)                                                                              \
  do {                                                                                              \
    g_checks++;                                                                                     \
    long long _a = (long long)(a), _b = (long long)(b);                                             \
    if (_a != _b) {                                                                                 \
      g_failed++;                                                                                   \
      printf("  FAIL %s:%d [%s] %s == %s (%lld != %lld)\n", __FILE__, __LINE__, g_test, #a, #b, _a, _b); \
    }                                                                                               \
  } while (0)

struct TestCase { const char* name; std::function<void()> fn; };
static std::vector<TestCase>& tests() { static std::vector<TestCase> t; return t; }
struct Reg { Reg(const char* n, std::function<void()> f) { tests().push_back({n, f}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

static bool logHas(const char* s) { return g_coreLogCapture.find(s) != std::string::npos; }

// ============================================================ KeyMatrix (pure decoding)

// Official M5Cardputer _key_value_map (value_first, value_second), 0 = special key.
static const char* const OFFICIAL_NORMAL[4] = {"`1234567890-=\b", "\tqwertyuiop[]\\", "\x01\x02" "asdfghjkl;'\n",
                                               "\x03\x04\x05zxcvbnm,./ "};
static const char* const OFFICIAL_SHIFT[4] = {"~!@#$%^&*()_+\b", "\tQWERTYUIOP{}|", "\x01\x02" "ASDFGHJKL:\"\n",
                                              "\x03\x04\x05ZXCVBNM<>? "};

static bool decode(KeyMatrix& m, uint8_t key, bool down, KeyEvent& e) { return m.feed(key | (down ? 0x80 : 0), e); }

TEST(keymatrix_every_key_matches_official_layout) {
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 14; c++) {
      uint8_t k = KeyMatrix::fromGrid(r, c);
      CHECK(k != 0);
      int rr, cc;
      CHECK(KeyMatrix::toGrid(k, rr, cc) && rr == r && cc == c);
      char want = OFFICIAL_NORMAL[r][c];
      if ((uint8_t)want < 0x06) continue;   // modifiers checked separately
      KeyMatrix m;
      KeyEvent e;
      bool got = decode(m, k, true, e);
      CHECK(got);
      if (want == '\b') CHECK(e.code == KeyCode::Backspace);
      else if (want == '\t') CHECK(e.code == KeyCode::Tab);
      else if (want == '\n') CHECK(e.code == KeyCode::Enter);
      else {
        CHECK(e.code == KeyCode::Char);
        if (e.ch != want) printf("  key r%d c%d: got '%c' want '%c'\n", r, c, e.ch, want);
        CHECK_EQ(e.ch, want);
      }
      // with Shift held
      KeyMatrix ms;
      KeyEvent es;
      decode(ms, KeyMatrix::fromGrid(2, 1), true, es);   // Shift down
      CHECK(decode(ms, k, true, es));
      char ws = OFFICIAL_SHIFT[r][c];
      if (ws != '\b' && ws != '\t' && ws != '\n') CHECK_EQ(es.ch, ws);
      CHECK(es.mods & MOD_SHIFT);
    }
  }
}

TEST(keymatrix_all_80_tca_numbers_safe) {
  // 7x8 matrix = 56 keys used; every other code must be ignored without crashing.
  int valid = 0;
  for (int k = 0; k < 128; k++) {
    int r, c;
    if (KeyMatrix::toGrid((uint8_t)k, r, c)) valid++;
    KeyMatrix m;
    KeyEvent e;
    m.feed((uint8_t)(k | 0x80), e);
  }
  CHECK_EQ(valid, 56);
}

TEST(keymatrix_fn_arrows_and_esc) {
  KeyMatrix m;
  KeyEvent e;
  decode(m, KeyMatrix::fromGrid(2, 0), true, e);   // Fn down
  CHECK(decode(m, KeyMatrix::fromGrid(2, 11), true, e) && e.code == KeyCode::Up);      // ;
  CHECK(decode(m, KeyMatrix::fromGrid(3, 11), true, e) && e.code == KeyCode::Down);    // .
  CHECK(decode(m, KeyMatrix::fromGrid(3, 10), true, e) && e.code == KeyCode::Left);    // ,
  CHECK(decode(m, KeyMatrix::fromGrid(3, 12), true, e) && e.code == KeyCode::Right);   // /
  CHECK(decode(m, KeyMatrix::fromGrid(0, 0), true, e) && e.code == KeyCode::Esc);      // `
  decode(m, KeyMatrix::fromGrid(2, 0), false, e);  // Fn up
  CHECK(decode(m, KeyMatrix::fromGrid(2, 11), true, e) && e.code == KeyCode::Char && e.ch == ';');
}

TEST(keymatrix_release_and_modifiers_do_not_emit) {
  KeyMatrix m;
  KeyEvent e;
  CHECK(!decode(m, KeyMatrix::fromGrid(1, 1), false, e));   // q released
  CHECK(!decode(m, KeyMatrix::fromGrid(2, 1), true, e));    // Shift
  CHECK(!decode(m, KeyMatrix::fromGrid(3, 0), true, e));    // Ctrl
  CHECK((m.mods() & (MOD_SHIFT | MOD_CTRL)) == (MOD_SHIFT | MOD_CTRL));
  decode(m, KeyMatrix::fromGrid(2, 1), false, e);
  CHECK((m.mods() & MOD_SHIFT) == 0);
}

// ============================================================ TCA8418 driver + fake chip

struct KbRig {
  FakeI2cBus bus;
  FakeTca8418 chip;
  Tca8418 drv;
  uint32_t t = 1000;
  explicit KbRig(bool useInt = true) : drv(bus, useInt ? &chip : nullptr) { bus.attach(0x34, &chip); }
  // Polls like the main loop does (every 2 ms) for `ms`, collecting key presses.
  std::vector<KeyEvent> run(uint32_t ms = 100) {
    std::vector<KeyEvent> out;
    for (uint32_t end = t + ms; t < end; t += 2) {
      KeyEvent e;
      while (drv.poll(e, t)) out.push_back(e);
    }
    return out;
  }
};

TEST(tca_begin_configures_7x8_matrix) {
  KbRig k;
  CHECK(k.drv.begin());
  CHECK(k.chip.configured());
  CHECK_EQ(k.chip.regs[0x01] & 0x01, 1);   // KE_IEN
  CHECK(k.chip.level());                   // INT idle high
}

TEST(tca_types_hello) {
  KbRig k;
  k.drv.begin();
  const char* word = "hello";
  for (const char* p = word; *p; p++) {
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 14; c++)
        if (OFFICIAL_NORMAL[r][c] == *p) k.chip.tap(r, c);
  }
  auto keys = k.run();
  std::string s;
  for (auto& e : keys) if (e.code == KeyCode::Char) s += e.ch;
  CHECK(s == "hello");
  CHECK_EQ(k.drv.eventsRead(), 10);   // 5 presses + 5 releases
  CHECK(k.chip.level());              // interrupt acknowledged
}

TEST(tca_shift_combo_through_chip) {
  KbRig k;
  k.drv.begin();
  k.chip.pressGrid(2, 1, true);   // Shift down
  k.chip.tap(1, 1);               // q
  k.chip.pressGrid(2, 1, false);
  k.chip.tap(1, 1);
  auto keys = k.run();
  CHECK_EQ(keys.size(), 2u);
  if (keys.size() == 2) { CHECK_EQ(keys[0].ch, 'Q'); CHECK_EQ(keys[1].ch, 'q'); }
}

TEST(tca_works_without_int_line) {
  KbRig k(false);   // INT not used: slow polling
  k.drv.begin();
  k.chip.tap(1, 2);   // w
  auto keys = k.run(120);
  CHECK_EQ(keys.size(), 1u);
  KbRig k2;          // INT pin configured but not wired (always high)
  k2.chip.intWired = false;
  k2.drv.begin();
  k2.chip.tap(1, 2);
  CHECK_EQ(k2.run(120).size(), 1u);
}

TEST(tca_absent_chip_is_reported_and_retried) {
  g_coreLogCapture.clear();
  KbRig k;
  k.chip.absent = true;
  CHECK(!k.drv.begin());
  CHECK(logHas("does not answer"));
  CHECK(k.run(100).empty());
  // the connector is fixed: the driver recovers by itself within RETRY_MS
  k.chip.absent = false;
  k.run(Tca8418::RETRY_MS + 100);
  CHECK(k.drv.ok());
  k.chip.tap(1, 3);
  CHECK_EQ(k.run().size(), 1u);
}

TEST(tca_dead_chip_detected_by_readback) {
  KbRig k;
  k.chip.ignoreWrites = true;
  CHECK(!k.drv.begin());
  CHECK(!k.drv.ok());
}

TEST(tca_boot_presses_flushed) {
  KbRig k;
  // events left in the FIFO before configuration (chip configured by a previous firmware)
  k.chip.regs[0x1D] = 0x7F; k.chip.regs[0x1E] = 0xFF;
  k.chip.tap(1, 1);
  k.chip.tap(1, 2);
  k.drv.begin();
  CHECK(k.chip.fifo.empty());
  CHECK(k.run().empty());
}

TEST(tca_fifo_overflow_counted) {
  KbRig k;
  k.drv.begin();
  for (int i = 0; i < 8; i++) k.chip.tap(1, 1);   // 16 events > 10-deep FIFO
  auto keys = k.run();
  CHECK_EQ(keys.size(), 5u);       // 10 events kept = 5 taps
  CHECK(k.drv.overflows() >= 1);
  CHECK(k.chip.level());
}

TEST(tca_transient_i2c_errors) {
  KbRig k;
  k.drv.begin();
  k.chip.tap(1, 4);
  k.chip.failNextReads = 3;
  auto keys = k.run(200);
  CHECK_EQ(keys.size(), 1u);
  CHECK(k.drv.ok());
  CHECK(k.drv.i2cErrors() >= 3);
}

TEST(tca_chip_lost_then_back) {
  KbRig k;
  k.drv.begin();
  k.chip.absent = true;
  k.run(300);   // 2 ms polling with INT high: slow poll every 50 ms -> errors pile up
  k.chip.absent = false;
  // power glitch: chip came back with reset registers
  k.chip.regs.assign(0x30, 0);
  k.run(Tca8418::RETRY_MS + 200);
  k.chip.tap(1, 5);
  auto keys = k.run();
  CHECK_EQ(keys.size(), 1u);
  CHECK(k.chip.configured());
}

// ============================================================ GPS driver + fake module

struct GpsRig {
  FakeGpsUart uart;
  FakeClock clock;
  GpsLink gps;
  uint32_t t = 0;
  uint32_t unix = 1767225600;   // 2026-01-01 00:00:00 UTC
  GpsRig() : gps(uart, &clock) {}
  // `secs` seconds of module output, the driver loop every 10 ms
  void run(int secs, bool fix = true, double lat = 43.6116, double lon = 1.3346) {
    for (int s = 0; s < secs; s++) {
      if (fix) uart.sendFix(lat, lon, 9, unix); else uart.sendNoFix(unix);
      for (int i = 0; i < 100; i++) { gps.loop(t); t += 10; }
      unix++;
    }
  }
};

TEST(gps_detects_115200_immediately) {
  GpsRig g;
  g.gps.begin(g.t);
  g.run(2);
  CHECK_EQ(g.gps.baud(), 115200);
  CHECK(g.gps.present(g.t));
  CHECK(g.gps.state().valid);
  CHECK(g.gps.state().lat > 43.61 && g.gps.state().lat < 43.62);
  CHECK(g.gps.state().lon > 1.33 && g.gps.state().lon < 1.34);
  CHECK_EQ(g.gps.state().sats, 9);
  CHECK_EQ(g.uart.opens, 1);
}

TEST(gps_finds_9600_module) {
  GpsRig g;
  g.uart.moduleBaud = 9600;
  g.gps.begin(g.t);
  g.run(8);
  CHECK_EQ(g.gps.baud(), 9600);
  CHECK(g.gps.state().valid);
}

TEST(gps_finds_38400_module) {
  GpsRig g;
  g.uart.moduleBaud = 38400;
  g.gps.begin(g.t);
  g.run(12);
  CHECK_EQ(g.gps.baud(), 38400);
}

TEST(gps_silent_module_keeps_searching_and_logs) {
  g_coreLogCapture.clear();
  GpsRig g;
  g.uart.silent = true;
  g.gps.begin(g.t);
  g.run(10);
  CHECK_EQ(g.gps.baud(), 0);
  CHECK(!g.gps.present(g.t));
  CHECK(g.uart.opens >= 3);
  CHECK(logHas("nothing received"));
  // module powered later: found at the next 115200 attempt
  g.uart.silent = false;
  g.run(10);
  CHECK_EQ(g.gps.baud(), 115200);
}

TEST(gps_bad_checksums_rejected) {
  GpsRig g;
  g.uart.corruptChecksums = true;
  g.gps.begin(g.t);
  g.run(3);
  CHECK(!g.gps.state().valid);
  CHECK(g.gps.state().checksumErrors > 0);
  CHECK_EQ(g.gps.baud(), 0);
}

TEST(gps_fix_lost_and_silence_detected) {
  g_coreLogCapture.clear();
  GpsRig g;
  g.gps.begin(g.t);
  g.run(2);
  CHECK(g.gps.state().valid);
  g.run(2, false);
  CHECK(!g.gps.state().valid);
  CHECK(logHas("fix lost"));
  g.uart.silent = true;
  g.run(7);
  CHECK(!g.gps.present(g.t));
  CHECK(logHas("module silent"));
}

// Real modules send RMC before GGA, and the driver may run in between (VALIDATION-7):
// the "fix" line must wait for GGA, otherwise it prints "0 sats, hdop 99.9".
TEST(gps_fix_log_waits_for_satellite_count) {
  g_coreLogCapture.clear();
  GpsRig g;
  g.gps.begin(g.t);
  g.run(2, false);                       // locked at 115200, no fix yet
  g.uart.send("GNRMC,000005.00,A,4336.69600,N,00120.07600,E,0.10,0.0,010126,,,A");
  for (int i = 0; i < 10; i++) { g.gps.loop(g.t); g.t += 10; }
  CHECK(g.gps.state().valid);
  CHECK(!logHas("fix 43"));              // position known, satellite count not yet
  g.uart.send("GNGGA,000005.00,4336.69600,N,00120.07600,E,1,07,1.2,150.0,M,49.6,M,,");
  for (int i = 0; i < 10; i++) { g.gps.loop(g.t); g.t += 10; }
  CHECK(logHas("fix 43.61160, 1.33460, 7 sats, hdop 1.2"));
  CHECK(!logHas("0 sats"));
}

// Indoors the module can report a position (RMC) while GGA still says "no satellites".
TEST(gps_fix_log_position_only) {
  g_coreLogCapture.clear();
  GpsRig g;
  g.gps.begin(g.t);
  g.run(2, false);
  for (int s = 0; s < 3; s++) {
    g.uart.send("GNRMC,000005.00,A,4336.69600,N,00120.07600,E,0.10,0.0,010126,,,A");
    g.uart.send("GNGGA,000005.00,,,,,0,00,99.9,,,,,,");
    for (int i = 0; i < 100; i++) { g.gps.loop(g.t); g.t += 10; }
  }
  CHECK(g.gps.state().valid);
  CHECK(logHas("fix 43.61160, 1.33460 (position only"));
  CHECK(!logHas("0 sats"));
  g.run(2, false);
  CHECK(logHas("fix lost"));
}

// A fix that flickers away before it was ever logged does not print a lone "fix lost".
TEST(gps_fix_flicker_not_logged) {
  g_coreLogCapture.clear();
  GpsRig g;
  g.gps.begin(g.t);
  g.run(2, false);
  g.uart.send("GNRMC,000005.00,A,4336.69600,N,00120.07600,E,0.10,0.0,010126,,,A");
  for (int i = 0; i < 10; i++) { g.gps.loop(g.t); g.t += 10; }
  g.run(1, false);
  CHECK(!g.gps.state().valid);
  CHECK(!logHas("fix 43"));
  CHECK(!logHas("fix lost"));
}

TEST(gps_sets_clock_quickly) {
  GpsRig g;
  g.clock.t = 1000;   // RTC not set at boot
  g.gps.begin(g.t);
  g.run(2);
  CHECK_EQ(g.clock.sets, 1);                       // first sync right away (not after 60 s)
  CHECK(g.clock.t >= 1767225600 && g.clock.t < 1767225600 + 5);
  g.run(5);
  CHECK_EQ(g.clock.sets, 1);                       // no drift: not set again
}

TEST(gps_byte_budget_bounded) {
  GpsRig g;
  g.gps.begin(g.t);
  for (int i = 0; i < 20; i++) g.uart.sendFix(43.6, 1.3, 8, g.unix);   // ~3 KB backlog
  size_t before = g.uart.rx.size();
  g.gps.loop(g.t);
  CHECK(before - g.uart.rx.size() <= (size_t)GpsLink::BYTES_PER_LOOP);
}

TEST(gps_cold_restart_command) {
  GpsRig g;
  g.gps.begin(g.t);
  g.gps.coldRestart();
  CHECK(g.uart.written.find("$PCAS10,2*") == 0);
}

// ============================================================ PI4IOE driver + fake expander

TEST(pi4ioe_sets_p0_high_glitch_free) {
  FakeI2cBus bus;
  FakePi4ioe chip;
  bus.attach(0x43, &chip);
  Pi4ioe io(bus);
  CHECK(io.probe());
  CHECK(io.setOutput(0, true));
  CHECK(chip.drives(0, true));
  // other pins untouched
  CHECK_EQ(chip.dir, 0x01);
  CHECK_EQ(chip.hiz, 0xFE);
  // order: level written before direction, high-Z released last
  int iOut = -1, iDir = -1, iHiz = -1;
  for (size_t i = 0; i < chip.writeLog.size(); i++) {
    if (chip.writeLog[i].first == 0x05 && iOut < 0) iOut = (int)i;
    if (chip.writeLog[i].first == 0x03 && iDir < 0) iDir = (int)i;
    if (chip.writeLog[i].first == 0x07 && iHiz < 0) iHiz = (int)i;
  }
  CHECK(iOut >= 0 && iOut < iDir && iDir < iHiz);
}

TEST(pi4ioe_absent_or_wrong_chip) {
  FakeI2cBus bus;
  FakePi4ioe chip;
  bus.attach(0x43, &chip);
  Pi4ioe io(bus);
  chip.absent = true;
  CHECK(!io.probe());
  chip.absent = false;
  chip.id = 0x12;   // another chip at 0x43
  CHECK(!io.probe());
  chip.id = 0xA2;
  chip.readsFail = true;
  CHECK(!io.probe());
}

// ============================================================ data partition probe (VALIDATION-8)

TEST(flashfs_classifies_partition_content) {
  uint8_t erased[64], lfs[64], spiffs[64];
  memset(erased, 0xFF, sizeof(erased));
  // LittleFS v2 superblock: revision count, tag, then the name "littlefs"
  memset(lfs, 0xFF, sizeof(lfs));
  const uint8_t head[] = {0x03, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0xFF, 0xF7, 'l', 'i', 't', 't', 'l', 'e', 'f', 's'};
  memcpy(lfs, head, sizeof(head));
  // SPIFFS object lookup page: object ids, no name
  memset(spiffs, 0xFF, sizeof(spiffs));
  const uint8_t ids[] = {0x01, 0x80, 0x01, 0x00, 0x02, 0x80, 0x02, 0x00};
  memcpy(spiffs, ids, sizeof(ids));

  CHECK(flashfs::classify(erased, erased, 64) == flashfs::Content::Blank);
  CHECK(flashfs::classify(lfs, erased, 64) == flashfs::Content::LittleFs);
  CHECK(flashfs::classify(erased, lfs, 64) == flashfs::Content::LittleFs);   // superblock copy in block 1
  CHECK(flashfs::classify(spiffs, erased, 64) == flashfs::Content::Foreign);
  CHECK(flashfs::classify(erased, spiffs, 64) == flashfs::Content::Foreign);
  CHECK(strstr(flashfs::describe(flashfs::Content::Foreign), "SPIFFS") != nullptr);
}

// ============================================================ nearby repeaters (RADIO-8)

// Answer of a repeater, built like meshcore/examples/simple_repeater/MyMesh.cpp
static size_t discoverAnswer(uint8_t* out, uint8_t id, int8_t snrX4, uint32_t tag, uint8_t type = 2) {
  out[0] = 0x90 | type;
  out[1] = (uint8_t)snrX4;
  memcpy(&out[2], &tag, 4);
  memset(&out[6], id, 8);
  return 14;
}

TEST(discovery_request_is_the_meshcore_one) {
  NodeDiscovery d;
  d.start(0x11223344, 0);
  uint8_t r[NodeDiscovery::REQUEST_LEN];
  d.buildRequest(r);
  const uint8_t want[] = {0x81, 0x04, 0x44, 0x33, 0x22, 0x11};
  CHECK(memcmp(r, want, sizeof(want)) == 0);
}

TEST(discovery_accepts_only_answers_to_this_search) {
  NodeDiscovery d;
  uint8_t a[20];
  size_t n = discoverAnswer(a, 1, -20, 7);
  CHECK(!d.accept(a, n, -9, -110, 0));                  // no search yet
  d.start(7, 1000);
  CHECK(!d.accept(a, n - 1, -9, -110, 1000));           // too short
  n = discoverAnswer(a, 1, -20, 8);
  CHECK(!d.accept(a, n, -9, -110, 1000));               // other tag
  n = discoverAnswer(a, 1, -20, 7, 4);
  CHECK(!d.accept(a, n, -9, -110, 1000));               // a sensor, not a repeater
  a[0] = 0x82;
  CHECK(!d.accept(a, 14, -9, -110, 1000));              // a request, not an answer
  CHECK_EQ(d.count(), 0);
  n = discoverAnswer(a, 1, -20, 7);
  CHECK(d.accept(a, n, -9.5f, -110, 2000));
  CHECK_EQ(d.count(), 1);
  CHECK(d.at(0).prefix[7] == 1);
  CHECK(d.at(0).type == 2);
  CHECK(d.at(0).snrThere == -5.0f);
  CHECK(d.at(0).snrHere == -9.5f);
  CHECK(d.at(0).rssi == -110.0f);
  CHECK_EQ(d.at(0).heardMs, 2000u);
}

TEST(discovery_sorts_merges_and_adds_up_searches) {
  NodeDiscovery d;
  uint8_t a[20];
  d.start(1, 0);
  d.accept(a, discoverAnswer(a, 1, 0, 1), -12, -112, 10);
  d.accept(a, discoverAnswer(a, 2, 0, 1), -3, -100, 20);
  CHECK_EQ(d.count(), 2);
  CHECK(d.at(0).prefix[0] == 2);
  d.accept(a, discoverAnswer(a, 1, 0, 1), 2, -95, 30);  // same repeater, better signal: no duplicate
  CHECK_EQ(d.count(), 2);
  CHECK(d.at(0).prefix[0] == 1);
  uint32_t t = NodeDiscovery::MIN_INTERVAL_MS;
  d.start(2, t);
  CHECK(!d.accept(a, discoverAnswer(a, 3, 0, 1), 0, -100, t));   // late answer to the previous search
  d.accept(a, discoverAnswer(a, 3, 0, 2), 0, -100, t);
  CHECK_EQ(d.count(), 3);                               // the first two are still listed
  d.clear();
  CHECK_EQ(d.count(), 0);
}

TEST(discovery_full_table_keeps_the_strongest) {
  NodeDiscovery d;
  uint8_t a[20];
  d.start(1, 0);
  for (int i = 0; i < NodeDiscovery::MAX_NODES; i++) d.accept(a, discoverAnswer(a, i + 1, 0, 1), -10 - i, -110, 0);
  CHECK_EQ(d.count(), NodeDiscovery::MAX_NODES);
  d.accept(a, discoverAnswer(a, 100, 0, 1), -99, -125, 0);   // weaker than all: dropped
  CHECK(d.at(NodeDiscovery::MAX_NODES - 1).prefix[0] == NodeDiscovery::MAX_NODES);
  d.accept(a, discoverAnswer(a, 101, 0, 1), 5, -90, 0);      // stronger: replaces the weakest
  CHECK_EQ(d.count(), NodeDiscovery::MAX_NODES);
  CHECK(d.at(0).prefix[0] == 101);
  CHECK(d.at(NodeDiscovery::MAX_NODES - 1).prefix[0] == NodeDiscovery::MAX_NODES - 1);
}

TEST(discovery_window_and_pause) {
  NodeDiscovery d;
  CHECK(d.canStart(0));
  CHECK(!d.searching(0));
  CHECK_EQ(d.secondsUntilNext(0), 0u);
  d.start(1, 5000);
  CHECK(d.searching(5000));
  CHECK_EQ(d.secondsLeft(5000), 15u);
  CHECK_EQ(d.secondsLeft(5001), 15u);
  CHECK(!d.canStart(5000));
  CHECK_EQ(d.secondsUntilNext(5000), 30u);
  CHECK(!d.searching(5000 + NodeDiscovery::WINDOW_MS));
  CHECK_EQ(d.secondsLeft(5000 + NodeDiscovery::WINDOW_MS), 0u);
  CHECK(!d.canStart(5000 + NodeDiscovery::MIN_INTERVAL_MS - 1));
  CHECK_EQ(d.secondsUntilNext(5000 + NodeDiscovery::MIN_INTERVAL_MS - 1), 1u);
  CHECK(d.canStart(5000 + NodeDiscovery::MIN_INTERVAL_MS));
}

// ============================================================ FloodOptions (RADIO-9, RADIO-10)

TEST(flood_path_bytes_stay_in_range) {
  using namespace floodopt;
  CHECK_EQ(validPathBytes(1), 1);
  CHECK_EQ(validPathBytes(3), 3);
  CHECK_EQ(validPathBytes(0), 1);     // a damaged file falls back to the compatible value
  CHECK_EQ(validPathBytes(4), 1);
  CHECK_EQ(validPathBytes(255), 1);
  CHECK_EQ(stepPathBytes(1, 1), 2);
  CHECK_EQ(stepPathBytes(3, 1), 1);
  CHECK_EQ(stepPathBytes(1, -1), 3);
  CHECK_EQ(stepPathBytes(0, 1), 2);
}

TEST(flood_region_names) {
  using namespace floodopt;
  char out[REGION_MAX + 1];
  CHECK(normalizeRegion("", out, sizeof(out)) == RegionName::None);
  CHECK(normalizeRegion("   ", out, sizeof(out)) == RegionName::None);
  CHECK(normalizeRegion("#", out, sizeof(out)) == RegionName::None);
  CHECK(normalizeRegion(nullptr, out, sizeof(out)) == RegionName::None);

  CHECK(normalizeRegion("fr", out, sizeof(out)) == RegionName::Ok);
  CHECK(strcmp(out, "fr") == 0);
  CHECK(normalizeRegion(" #eu ", out, sizeof(out)) == RegionName::Ok);
  CHECK(strcmp(out, "eu") == 0);
  CHECK(normalizeRegion("FR", out, sizeof(out)) == RegionName::Ok);   // the key depends on the case: kept
  CHECK(strcmp(out, "FR") == 0);
  CHECK(normalizeRegion("fr-31", out, sizeof(out)) == RegionName::Ok);
  CHECK(normalizeRegion("Île-de-France", out, sizeof(out)) == RegionName::Ok);

  CHECK(normalizeRegion("$club", out, sizeof(out)) == RegionName::Private);
  CHECK(out[0] == 0);
  CHECK(normalizeRegion("fr eu", out, sizeof(out)) == RegionName::Invalid);
  CHECK(normalizeRegion("*", out, sizeof(out)) == RegionName::Invalid);
  CHECK(normalizeRegion("a.b", out, sizeof(out)) == RegionName::Invalid);
  CHECK(out[0] == 0);
  std::string max(REGION_MAX, 'x');
  CHECK(normalizeRegion(max.c_str(), out, sizeof(out)) == RegionName::Ok);
  CHECK(normalizeRegion((max + "x").c_str(), out, sizeof(out)) == RegionName::Invalid);
}

// Settings files saved before these options hold the first 44 bytes of UiSettings: they must load with new defaults,
// and the padding byte of that older layout must not land in a new field.
static_assert(offsetof(UiSettings, language) == 42, "fields of the older settings file moved");
static_assert(offsetof(UiSettings, reserved0) == 43, "reserved0 must sit on the old padding byte");
static_assert(offsetof(UiSettings, pathHashBytes) == 44, "new fields start after the older layout");

TEST(older_settings_files_keep_the_flood_defaults) {
  UiSettings older;
  strcpy(older.nodeName, "Saved");
  uint8_t file[44];
  memcpy(file, &older, sizeof(file));
  file[43] = 0xA5;                       // whatever the padding byte held
  UiSettings s;                          // what DeviceSettings::load does
  memcpy((void*)&s, file, sizeof(file));   // raw bytes, as the device reads them
  CHECK(strcmp(s.nodeName, "Saved") == 0);
  CHECK_EQ(s.pathHashBytes, 1);
  CHECK_EQ(s.region[0], 0);
}

// ============================================================ AirtimeMeter (RADIO-4)

TEST(airtime_limits_of_the_presets) {
  CHECK_EQ(AirtimeMeter::limitPercent(869.618f), 10);    // MeshCore EU narrow
  CHECK_EQ(AirtimeMeter::limitPercent(869.525f), 10);
  CHECK_EQ(AirtimeMeter::limitPercent(868.3f), 1);
  CHECK_EQ(AirtimeMeter::limitPercent(869.85f), 1);
  CHECK(AirtimeMeter::limitPercent(868.9f) < 0.2f);
  CHECK(AirtimeMeter::limitPercent(864.0f) < 0.2f);
  CHECK_EQ(AirtimeMeter::limitPercent(915.0f), 0);       // no table outside Europe: no alert
}

TEST(airtime_counts_the_last_hour) {
  AirtimeMeter m;
  m.update(5000, 1000);                 // what was sent before the first call does not count
  CHECK_EQ(m.lastHourMs(), 0u);
  m.update(5600, 2000);
  CHECK_EQ(m.lastHourMs(), 600u);
  m.update(6600, 30 * 60000);
  CHECK_EQ(m.lastHourMs(), 1600u);
  CHECK(m.percent() > 0.04f && m.percent() < 0.05f);
  m.update(6600, 59 * 60000);
  CHECK_EQ(m.lastHourMs(), 1600u);
  m.update(6600, 61 * 60000 + 2000);    // the first minute left the window
  CHECK_EQ(m.lastHourMs(), 1000u);
  m.update(6600, 200 * 60000);          // long silence: everything left
  CHECK_EQ(m.lastHourMs(), 0u);
}

TEST(airtime_survives_counter_and_clock_rollover) {
  AirtimeMeter m;
  uint32_t t = 0xFFFFFFFFu - 30000;
  uint32_t total = 0xFFFFFFFFu - 100;
  m.update(total, t);
  m.update(total + 300, t + 60000 + 1000);    // both wrap around 2^32
  CHECK_EQ(m.lastHourMs(), 300u);
}

TEST(airtime_one_minute_cannot_exceed_a_minute) {
  AirtimeMeter m;
  m.update(0, 0);
  m.update(500000, 1000);
  CHECK_EQ(m.lastHourMs(), AirtimeMeter::BUCKET_MS);
}

TEST(airtime_alerts_once_with_hysteresis) {
  using A = AirtimeMeter::Alert;
  AirtimeMeter m;
  uint32_t total = 0, t = 0;
  m.update(0, 0);
  const auto minute = [&](uint32_t ms) { total += ms; t += 60000; m.update(total, t); };   // one minute of the radio's life
  minute(60000);  minute(60000);                    // 3.3 %
  CHECK(m.pollAlert(10) == A::None);
  minute(60000);  minute(60000);                    // 6.7 %
  CHECK(m.pollAlert(0) == A::None);                 // no known limit: silent
  CHECK(m.pollAlert(10) == A::None);
  minute(48000);                                    // 8.0 %
  CHECK(m.pollAlert(10) == A::Near);
  CHECK(m.pollAlert(10) == A::None);                // reported once
  minute(60000);                                    // 9.7 %
  CHECK(m.pollAlert(10) == A::None);
  minute(12000);                                    // 10.0 %
  CHECK(m.pollAlert(10) == A::Over);
  CHECK(m.pollAlert(10) == A::None);
  // the hour slides: below the limit but above 80 %, no new warning, and Over does not repeat when hovering
  AirtimeMeter h;
  h.update(0, 0);
  h.update(60000, 1000); h.update(120000, 62000); h.update(180000, 123000); h.update(240000, 184000);
  h.update(300000, 245000); h.update(360000, 306000);
  CHECK(h.pollAlert(10) == A::Over);
  h.update(360000, 59 * 60000 + 2000 + 60000);   // the first minute left: 8.3 %
  CHECK(h.pollAlert(10) == A::None);
  h.update(360000, 200 * 60000);               // all gone: armed again
  CHECK(h.pollAlert(10) == A::None);
  h.update(420000, 201 * 60000); h.update(480000, 202 * 60000); h.update(540000, 203 * 60000);
  h.update(600000, 204 * 60000); h.update(660000, 205 * 60000);
  CHECK(h.pollAlert(10) == A::Near);
}

// ============================================================ PacketLog (RADIO-6)

static PacketLog::Frame frame(uint8_t route, uint8_t type, uint8_t pathLen, const uint8_t* path, const std::vector<uint8_t>& body) {
  PacketLog::Frame f;
  f.header = wire::header(route, type);
  f.pathLen = pathLen;
  f.path = path;
  f.payload = body.data();
  f.payloadLen = body.size();
  f.rawLen = 2 + (int)body.size();
  f.snr = -9.25f;
  f.rssi = -112;
  f.ms = 1234;
  f.unixTime = 1793541600;
  return f;
}

TEST(packet_log_decodes_each_kind) {
  PacketLog log;
  const uint8_t path[] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
  std::vector<uint8_t> txt = {0x11, 0x22, 0, 0, 9, 9};
  log.add(frame(wire::ROUTE_FLOOD, wire::PL_TXT, 2, path, txt));
  const PacketRecord* r = log.recent(0);
  CHECK(r && r->type == wire::PL_TXT && r->route == wire::ROUTE_FLOOD && r->flood());
  CHECK_EQ(r->hops, 2);
  CHECK_EQ(r->hashBytes, 1);
  CHECK_EQ(r->to, 0x11);
  CHECK(r->flags & PacketRecord::HAS_TO);
  CHECK_EQ(r->from[0], 0x22);
  CHECK_EQ(r->fromLen, 1);
  CHECK_EQ(r->lastHop[0], 0xB2);        // the repeater that sent it to us: last entry of the path
  CHECK_EQ(r->snrX4, -37);
  CHECK_EQ(r->rssi, -112);

  std::vector<uint8_t> adv = {0x61, 0x20, 0x18, 0x69, 1, 2, 3};
  log.add(frame(wire::ROUTE_FLOOD, wire::PL_ADVERT, (1 << 6) | 2, path, adv));   // 2 hops, 2 bytes each
  r = log.recent(0);
  CHECK_EQ(r->hops, 2);
  CHECK_EQ(r->hashBytes, 2);
  CHECK_EQ(r->lastHop[0], 0xC3);
  CHECK_EQ(r->lastHop[1], 0xD4);
  CHECK_EQ(r->fromLen, 3);
  CHECK_EQ(r->from[2], 0x18);
  CHECK(r->source == PacketRecord::SRC_NODE);

  std::vector<uint8_t> grp = {0x7C, 0, 0, 0};
  log.add(frame(wire::ROUTE_FLOOD, wire::PL_GRP_TXT, (2 << 6) | 1, path, grp));  // 1 hop, 3 bytes
  r = log.recent(0);
  CHECK(r->source == PacketRecord::SRC_CHANNEL);
  CHECK_EQ(r->from[0], 0x7C);
  CHECK_EQ(r->lastHop[2], 0xC3);

  std::vector<uint8_t> ack = {1, 2, 3, 4};
  log.add(frame(wire::ROUTE_DIRECT, wire::PL_ACK, 0, path, ack));
  r = log.recent(0);
  CHECK(r->source == PacketRecord::SRC_NONE);
  CHECK(!r->flood());

  std::vector<uint8_t> anon = {0x33, 0x44, 0x55, 0x66, 0x77};
  log.add(frame(wire::ROUTE_FLOOD, wire::PL_ANON_REQ, 0, path, anon));
  r = log.recent(0);
  CHECK_EQ(r->to, 0x33);
  CHECK_EQ(r->from[0], 0x44);
  CHECK_EQ(r->fromLen, 3);

  std::vector<uint8_t> found(14, 0);
  found[0] = 0x92; found[6] = 0xAB; found[7] = 0xCD; found[8] = 0xEF;
  log.add(frame(wire::ROUTE_DIRECT, wire::PL_CONTROL, 0, path, found));
  r = log.recent(0);
  CHECK_EQ(r->from[0], 0xAB);
  std::vector<uint8_t> request = {0x81, 0x04, 1, 2, 3, 4};
  log.add(frame(wire::ROUTE_DIRECT, wire::PL_CONTROL, 0, path, request));
  CHECK(log.recent(0)->source == PacketRecord::SRC_NONE);   // a request has no key in it
}

TEST(packet_log_short_payloads_are_safe) {
  PacketLog log;
  std::vector<uint8_t> one = {0x11}, none;
  log.add(frame(wire::ROUTE_FLOOD, wire::PL_TXT, 0, nullptr, one));
  CHECK_EQ(log.recent(0)->fromLen, 0);
  log.add(frame(wire::ROUTE_FLOOD, wire::PL_ADVERT, 0, nullptr, none));
  CHECK_EQ(log.recent(0)->fromLen, 0);
  log.add(frame(wire::ROUTE_FLOOD, wire::PL_GRP_TXT, 0, nullptr, none));
  CHECK(log.recent(0)->source == PacketRecord::SRC_NONE);
  log.add(frame(wire::ROUTE_DIRECT, wire::PL_CONTROL, 0, nullptr, one));
  CHECK(log.recent(0)->source == PacketRecord::SRC_NONE);
  std::vector<uint8_t> two = {0x90, 0};
  log.add(frame(wire::ROUTE_DIRECT, wire::PL_CONTROL, 0, nullptr, two));
  CHECK(log.recent(0)->source == PacketRecord::SRC_NONE);
  PacketLog::Frame bad = frame(wire::ROUTE_FLOOD, wire::PL_ACK, (3 << 6) | 1, (const uint8_t*)"abcd", one);   // reserved size
  log.add(bad);
  CHECK_EQ(log.recent(0)->hashBytes, 1);
}

TEST(packet_log_region_flags) {
  PacketLog log;
  std::vector<uint8_t> body = {1, 2};
  PacketLog::Frame f = frame(wire::ROUTE_TRANSPORT_FLOOD, wire::PL_TXT, 0, nullptr, body);
  f.transportCode = 0xBEEF;
  f.ourRegion = true;
  log.add(f);
  CHECK(log.recent(0)->scoped() && log.recent(0)->ourRegion() && log.recent(0)->flood());
  CHECK_EQ(log.recent(0)->transportCode, 0xBEEF);
  f.ourRegion = false;
  log.add(f);
  CHECK(log.recent(0)->scoped() && !log.recent(0)->ourRegion());
  log.add(frame(wire::ROUTE_FLOOD, wire::PL_TXT, 0, nullptr, body));
  CHECK(!log.recent(0)->scoped());
}

TEST(packet_log_ring) {
  PacketLog log;
  std::vector<uint8_t> body = {1, 2};
  CHECK_EQ(log.count(), 0);
  CHECK(log.recent(0) == nullptr);
  for (int i = 0; i < PacketLog::CAPACITY + 5; i++) log.add(frame(wire::ROUTE_FLOOD, wire::PL_TXT, 0, nullptr, body));
  CHECK_EQ(log.count(), PacketLog::CAPACITY);
  CHECK_EQ(log.total(), (uint32_t)PacketLog::CAPACITY + 5);
  CHECK_EQ(log.recent(0)->seq, (uint32_t)PacketLog::CAPACITY + 4);
  CHECK_EQ(log.recent(PacketLog::CAPACITY - 1)->seq, 5u);
  CHECK(log.recent(PacketLog::CAPACITY) == nullptr);
  CHECK(log.bySeq(4) == nullptr);       // overwritten
  CHECK(log.bySeq(5) != nullptr && log.bySeq(5)->seq == 5);
  CHECK(log.bySeq(log.total()) == nullptr);
  log.clear();
  CHECK_EQ(log.count(), 0);
  CHECK_EQ(log.total(), 0u);
}

TEST(packet_log_csv) {
  PacketLog log;
  const uint8_t path[] = {0xC3, 0xD4};
  std::vector<uint8_t> body = {0x11, 0x22};
  log.add(frame(wire::ROUTE_FLOOD, wire::PL_TXT, (1 << 6) | 1, path, body));
  char line[PacketLog::CSV_LINE_MAX];
  PacketLog::formatCsv(*log.recent(0), line, sizeof(line));
  CHECK(std::string(line) == "0,1234,1793541600,rx,TXT,F,1,2,22,11,c3d4,-9.25,-112,4,");
  // the widest line fits
  PacketRecord w;
  w.seq = 4294967295u; w.ms = 4294967295u; w.unixTime = 4294967295u; w.type = wire::PL_MULTIPART; w.route = wire::ROUTE_TRANSPORT_DIRECT;
  w.hops = 63; w.hashBytes = 3; w.fromLen = 3; w.flags = PacketRecord::HAS_TO | PacketRecord::HAS_CODE; w.snrX4 = -128; w.rssi = -32768; w.len = 255;
  PacketLog::formatCsv(w, line, sizeof(line));
  CHECK(strlen(line) < sizeof(line) - 1);
  CHECK(std::string(line).find("other") != std::string::npos);
  int commas = 0;
  for (const char* c = PacketLog::csvHeader(); *c; c++) commas += *c == ',';
  int got = 0;
  for (const char* c = line; *c; c++) got += *c == ',';
  CHECK_EQ(commas, got);
}

// ============================================================ PathView, routes in messages (MESSAGING-10)

TEST(path_view_reads_each_size) {
  const uint8_t p[] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
  PathView one(3, p, sizeof(p));
  CHECK_EQ(one.hops(), 3);
  CHECK_EQ(one.hashBytes(), 1);
  CHECK_EQ(one.hash(2)[0], 0xC3);
  PathView two((1 << 6) | 2, p, sizeof(p));
  CHECK_EQ(two.hops(), 2);
  CHECK_EQ(two.hashBytes(), 2);
  CHECK_EQ(two.hash(1)[1], 0xD4);
  PathView three((2 << 6) | 2, p, sizeof(p));
  CHECK_EQ(three.hash(1)[2], 0xF6);
  CHECK(!three.truncated());
  PathView reserved((3 << 6) | 1, p, sizeof(p));
  CHECK(!reserved.validSize());
  CHECK_EQ(reserved.heldHops(), 0);
  PathView none(2, nullptr, 8);
  CHECK_EQ(none.heldHops(), 0);
  CHECK(none.truncated());
}

TEST(path_view_text) {
  const uint8_t p[] = {0xA1, 0xB2, 0xC3, 0xD4};
  char out[32];
  PathView(0, p, sizeof(p)).format(out, sizeof(out));
  CHECK(strcmp(out, "") == 0);
  PathView(3, p, sizeof(p)).format(out, sizeof(out));
  CHECK(strcmp(out, "a1>b2>c3") == 0);
  PathView((1 << 6) | 2, p, sizeof(p)).format(out, sizeof(out));
  CHECK(strcmp(out, "a1b2>c3d4") == 0);
  PathView(5, p, sizeof(p)).format(out, sizeof(out));       // more hops than the bytes held
  CHECK(strcmp(out, "a1>b2>c3>d4..") == 0);
  char tiny[8];
  PathView(4, p, sizeof(p)).format(tiny, sizeof(tiny));
  CHECK(strlen(tiny) < sizeof(tiny));
  CHECK(strstr(tiny, "..") != nullptr);
}

TEST(route_copies_and_reverses) {
  const uint8_t p[] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
  MsgRoute r;
  CHECK(!r.known());
  r.set(PATH_TAKEN, (1 << 6) | 3, p);
  CHECK(r.known() && r.kind == PATH_TAKEN);
  CHECK_EQ(r.view().hops(), 3);
  CHECK_EQ(r.view().hash(2)[1], 0xF6);
  r.setReversed(PATH_PLANNED, (1 << 6) | 3, p);    // entries reverse, the bytes of each ID do not
  CHECK_EQ(r.view().hash(0)[0], 0xE5);
  CHECK_EQ(r.view().hash(0)[1], 0xF6);
  CHECK_EQ(r.view().hash(2)[1], 0xB2);
  r.set(PATH_TAKEN, 0, nullptr);                   // heard directly: a known route of no hop
  CHECK(r.known());
  CHECK_EQ(r.view().hops(), 0);
  r.set(PATH_TAKEN, (3 << 6) | 1, p);              // reserved size
  CHECK(!r.known());
  r.set(PATH_TAKEN, 2, nullptr);                   // no data
  CHECK(!r.known());
}

TEST(route_longer_than_a_record_is_cut) {
  uint8_t p[64];
  for (int i = 0; i < 64; i++) p[i] = (uint8_t)i;
  MsgRoute r;
  r.set(PATH_TAKEN, 40, p);
  CHECK_EQ(r.view().hops(), 40);
  CHECK_EQ(r.view().heldHops(), MSG_PATH_MAX);
  CHECK(r.view().truncated());
  CHECK_EQ(r.view().hash(31)[0], 31);              // from the sender on
  r.set(PATH_TAKEN, (2 << 6) | 21, p);             // 3 bytes: 63 bytes in the packet, 10 hops held
  CHECK_EQ(r.view().heldHops(), 10);
  CHECK_EQ(r.view().hash(9)[2], 29);
  r.setReversed(PATH_PLANNED, 40, p);              // reversed: starts at the far end of the source
  CHECK_EQ(r.view().hash(0)[0], 39);
  CHECK_EQ(r.view().hash(31)[0], 8);
}

// ============================================================ MessageStore

class MemoryStorage : public Storage {
public:
  bool read(const char* path, uint32_t off, void* buf, uint32_t len) override {
    auto it = files.find(path);
    if (it == files.end() || off + len > it->second.size()) return false;
    memcpy(buf, it->second.data() + off, len);
    return true;
  }
  bool write(const char* path, uint32_t off, const void* buf, uint32_t len) override {
    std::vector<uint8_t>& f = files[path];
    if (f.size() < off + len) f.resize(off + len);
    memcpy(f.data() + off, buf, len);
    return true;
  }
  bool remove(const char* path) override { return files.erase(path) > 0; }
  bool rename(const char* from, const char* to) override {
    auto it = files.find(from);
    if (it == files.end()) return false;
    files[to] = it->second;
    files.erase(from);
    return true;
  }
  std::map<std::string, std::vector<uint8_t>> files;
};

static ConvKey directConv(uint8_t b) {
  ConvKey k;
  k.kind = ConvKey::DIRECT;
  k.id[0] = b;
  return k;
}

TEST(store_keeps_the_route_of_a_message) {
  MemoryStorage disk;
  MessageStore st;
  st.begin(&disk);
  StoredMsg m;
  m.rssi = -113;
  m.routeType = wire::ROUTE_FLOOD;
  const uint8_t path[] = {0xA1, 0xB2};
  m.route.set(PATH_TAKEN, 2, path);
  strcpy(m.text, "hi");
  ConvKey k = directConv(1);
  CHECK(st.append(k, "Bob", m, true) == 0);
  CHECK(st.flush());

  MessageStore reloaded;               // after a restart
  reloaded.begin(&disk);
  StoredMsg r;
  CHECK(reloaded.read(k, 0, r));
  CHECK(r.route.kind == PATH_TAKEN);
  CHECK_EQ(r.route.view().hash(1)[0], 0xB2);
  CHECK_EQ(r.rssi, -113);
  CHECK(strcmp(r.text, "hi") == 0);
}

TEST(store_ring_keeps_the_newest_routes) {
  MemoryStorage disk;
  MessageStore st;
  st.begin(&disk);
  ConvKey k = directConv(2);
  for (int i = 0; i < MessageStore::MAX_MSGS + 3; i++) {
    StoredMsg m;
    uint8_t path[1] = {(uint8_t)i};
    m.route.set(PATH_TAKEN, 1, path);
    CHECK(st.append(k, "Bob", m, false) == i);
  }
  StoredMsg out[2];
  CHECK_EQ(st.loadRecent(k, out, 2, 0), 2);
  CHECK_EQ(out[1].route.view().hash(0)[0], MessageStore::MAX_MSGS + 2);
  CHECK_EQ(out[0].route.view().hash(0)[0], MessageStore::MAX_MSGS + 1);
}

TEST(store_clears_the_older_format) {
  MemoryStorage disk;
  // What the previous firmware left: its index, two ring files, and the other files of the partition
  struct { uint32_t magic, recordSize, count; } header = {0x31495043, (uint32_t)sizeof(ConvMeta), 2};
  ConvMeta metas[2];
  metas[0].key = directConv(3);
  metas[1].key = directConv(4);
  disk.write("/mindex", 0, &header, sizeof(header));
  disk.write("/mindex", sizeof(header), metas, sizeof(metas));
  char a[24], b[24];
  metas[0].key.path(a, sizeof(a));
  metas[1].key.path(b, sizeof(b));
  disk.write(a, 0, "old", 3);
  disk.write(b, 0, "old", 3);
  disk.write("/contacts.cp", 0, "keep", 4);

  MessageStore st;
  st.begin(&disk);
  CHECK_EQ(st.count(), 0);
  CHECK(!disk.files.count(a) && !disk.files.count(b) && !disk.files.count("/mindex"));
  CHECK(disk.files.count("/contacts.cp"));
  StoredMsg m;
  CHECK(st.append(directConv(3), "Bob", m, false) == 0);   // and it works afterwards
}

TEST(store_replaces_a_ring_file_of_another_format) {
  MemoryStorage disk;
  ConvKey k = directConv(5);
  char path[24];
  k.path(path, sizeof(path));
  uint32_t old[4] = {0x314D5043, 7, 196, 64};              // no index, an orphan file
  disk.write(path, 0, old, sizeof(old));
  disk.write(path, 5000, "x", 1);
  MessageStore st;
  st.begin(&disk);
  StoredMsg m;
  CHECK(st.append(k, "Bob", m, false) == 0);               // numbering does not continue from the orphan
  CHECK_EQ(st.total(k), 1u);
}

TEST(store_capacity_follows_the_flash) {
  MessageStore st;
  CHECK(MessageStore::FLASH_KB_PER_CONV * 1024 > MessageStore::MAX_MSGS * (int)sizeof(StoredMsg));
  st.setCapacityFromFlash(5500);       // the 5.4 MB partition
  CHECK_EQ(st.capacity(), MessageStore::MAX_CONVS);
  st.setCapacityFromFlash(500);        // what a launcher may leave: fewer conversations
  CHECK_EQ(st.capacity(), (500 - MessageStore::RESERVED_FLASH_KB) / MessageStore::FLASH_KB_PER_CONV);
  st.setCapacityFromFlash(10);
  CHECK_EQ(st.capacity(), 4);
}

// ============================================================ NodeLookup, RouteResolver (MESSAGING-10)

class FakeNodes : public NodeDirectory {
public:
  int contactCount() override { return (int)nodes.size(); }
  bool contactAt(int i, ContactView& out) override { out = nodes[i]; return true; }
  void add(const char* name, uint8_t k0, uint8_t k1, uint8_t type, bool hasPos = false, double lat = 0, double lon = 0) {
    ContactView c{};
    utf8Copy(c.name, sizeof(c.name), name);
    c.pubKey[0] = k0;
    c.pubKey[1] = k1;
    c.key.kind = ConvKey::DIRECT;
    c.key.id[0] = (uint8_t)nodes.size();
    c.type = type;
    c.hasPos = hasPos;
    c.latE6 = (int32_t)(lat * 1e6);
    c.lonE6 = (int32_t)(lon * 1e6);
    nodes.push_back(c);
  }
  std::vector<ContactView> nodes;
};

TEST(node_lookup_returns_every_candidate) {
  FakeNodes d;
  d.add("Alpha", 0xA1, 0x01, CONTACT_REPEATER);
  d.add("Beta", 0xA1, 0x02, CONTACT_REPEATER);
  d.add("Person", 0xA1, 0x03, CONTACT_CHAT);
  d.add("Room", 0xA1, 0x04, CONTACT_ROOM);
  d.add("Gamma", 0xB2, 0x05, CONTACT_REPEATER);
  NodeLookup look(d);
  NodeMatch m[2];
  const uint8_t a1[] = {0xA1, 0x02}, none[] = {0xCC};
  CHECK_EQ(look.find(a1, 1, NodeLookup::Kind::Relay, m, 2), 3);     // more than it can return, and it says so
  CHECK(strcmp(m[1].name, "Beta") == 0);
  CHECK_EQ(look.find(a1, 1, NodeLookup::Kind::Any, m, 2), 4);       // a person does not relay
  CHECK_EQ(look.find(a1, 2, NodeLookup::Kind::Relay, m, 2), 1);     // two bytes settle it
  CHECK_EQ(look.find(none, 1, NodeLookup::Kind::Any, m, 2), 0);
  char name[16] = "";
  CHECK(look.uniqueName(a1, 2, NodeLookup::Kind::Relay, name, sizeof(name)));
  CHECK(strcmp(name, "Beta") == 0);
  CHECK(!look.uniqueName(a1, 1, NodeLookup::Kind::Relay, name, sizeof(name)));
}

static RouteEndpoint endpoint(const char* name, bool hasPos = false, double lat = 0, double lon = 0) {
  RouteEndpoint e;
  e.name = name;
  e.hasPos = hasPos;
  e.lat = lat;
  e.lon = lon;
  return e;
}

static MsgRoute takenRoute(std::vector<uint8_t> bytes, int hops, int hashBytes = 1) {
  MsgRoute r;
  r.set(PATH_TAKEN, (uint8_t)(((hashBytes - 1) << 6) | hops), bytes.data());
  return r;
}

TEST(route_points_keep_the_travel_order) {
  FakeNodes d;
  d.add("Near", 0x11, 0, CONTACT_REPEATER, true, 43.6, 1.4);
  d.add("Far", 0x22, 0, CONTACT_REPEATER, true, 43.7, 1.5);
  static ResolvedRoute r;
  RouteResolver(d).resolve(takenRoute({0x22, 0x11}, 2), endpoint("Bob", true, 43.8, 1.6), endpoint("Me", true, 43.5, 1.3), r);
  CHECK_EQ(r.count, 4);
  CHECK(strcmp(r.points[0].name, "Bob") == 0 && r.points[0].state == HopState::Endpoint);
  CHECK(strcmp(r.points[1].name, "Far") == 0 && r.points[1].state == HopState::Single);
  CHECK(strcmp(r.points[2].name, "Near") == 0);
  CHECK(strcmp(r.points[3].name, "Me") == 0);
  CHECK_EQ(r.placed, 2);
  CHECK_EQ(r.ambiguous, 0);
  CHECK_EQ(r.unknown, 0);
  CHECK(r.kind == PATH_TAKEN);
}

TEST(route_unknown_hops_and_missing_positions) {
  FakeNodes d;
  d.add("NoPosition", 0x11, 0, CONTACT_REPEATER);
  static ResolvedRoute r;
  RouteResolver(d).resolve(takenRoute({0x11, 0x99}, 2), endpoint("Bob"), endpoint("Me"), r);
  CHECK(r.points[1].state == HopState::Single && !r.points[1].hasPos);
  CHECK(strcmp(r.points[1].name, "NoPosition") == 0);
  CHECK(r.points[2].state == HopState::Unknown && !r.points[2].hasPos);
  CHECK_EQ(r.points[2].hash[0], 0x99);
  CHECK_EQ(r.placed, 0);
  CHECK_EQ(r.unknown, 1);
}

TEST(route_ambiguous_hop_takes_the_candidate_on_the_way) {
  FakeNodes d;
  d.add("Lille", 0x11, 0, CONTACT_REPEATER, true, 50.63, 3.06);      // the same ID, far away
  d.add("Colomiers", 0x11, 1, CONTACT_REPEATER, true, 43.61, 1.33);
  static ResolvedRoute r;
  RouteResolver(d).resolve(takenRoute({0x11}, 1), endpoint("Bob", true, 43.60, 1.44), endpoint("Me", true, 43.62, 1.20), r);
  CHECK(r.points[1].state == HopState::Ambiguous);
  CHECK(strcmp(r.points[1].name, "Colomiers") == 0);
  CHECK(r.points[1].hasPos);
  CHECK_EQ(r.points[1].candidates, 2);
  CHECK_EQ(r.ambiguous, 1);
  CHECK_EQ(r.placed, 1);
}

TEST(route_ambiguous_hops_in_a_row_follow_each_other) {
  FakeNodes d;
  d.add("West", 0x11, 0, CONTACT_REPEATER, true, 43.6, 1.0);
  d.add("Paris", 0x11, 1, CONTACT_REPEATER, true, 48.8, 2.3);
  d.add("Middle", 0x22, 0, CONTACT_REPEATER, true, 43.6, 1.1);
  d.add("Lyon", 0x22, 1, CONTACT_REPEATER, true, 45.7, 4.8);
  static ResolvedRoute r;
  RouteResolver(d).resolve(takenRoute({0x11, 0x22}, 2), endpoint("Bob", true, 43.6, 0.9), endpoint("Me", false), r);
  CHECK(strcmp(r.points[1].name, "West") == 0);
  CHECK(strcmp(r.points[2].name, "Middle") == 0);    // close to the choice made just before
}

TEST(route_ambiguous_hop_with_nothing_to_compare) {
  FakeNodes d;
  d.add("NoPos", 0x11, 0, CONTACT_REPEATER);
  d.add("First", 0x11, 1, CONTACT_REPEATER, true, 43.6, 1.0);
  d.add("Second", 0x11, 2, CONTACT_REPEATER, true, 44.0, 2.0);
  static ResolvedRoute r;
  RouteResolver(d).resolve(takenRoute({0x11}, 1), endpoint("Bob"), endpoint("Me"), r);
  CHECK(strcmp(r.points[1].name, "First") == 0);     // a stable choice, still shown as ambiguous
  CHECK(r.points[1].state == HopState::Ambiguous);
  CHECK_EQ(r.points[1].candidates, 3);
  FakeNodes blind;
  blind.add("A", 0x11, 0, CONTACT_REPEATER);
  blind.add("B", 0x11, 1, CONTACT_REPEATER);
  RouteResolver(blind).resolve(takenRoute({0x11}, 1), endpoint("Bob"), endpoint("Me"), r);
  CHECK(r.points[1].state == HopState::Ambiguous && !r.points[1].hasPos);
  CHECK_EQ(r.placed, 0);
}

TEST(route_two_byte_ids_are_not_confused) {
  FakeNodes d;
  d.add("A", 0x11, 0xAA, CONTACT_REPEATER, true, 43.6, 1.0);
  d.add("B", 0x11, 0xBB, CONTACT_REPEATER, true, 44.0, 2.0);
  static ResolvedRoute r;
  RouteResolver(d).resolve(takenRoute({0x11, 0xBB}, 1, 2), endpoint("Bob"), endpoint("Me"), r);
  CHECK(r.points[1].state == HopState::Single);
  CHECK(strcmp(r.points[1].name, "B") == 0);
}

TEST(route_without_hops_or_cut_short) {
  FakeNodes d;
  static ResolvedRoute r;
  MsgRoute none;
  RouteResolver(d).resolve(none, endpoint("Bob"), endpoint("Me"), r);
  CHECK_EQ(r.count, 2);                 // the two ends, no hop
  CHECK(r.kind == PATH_NONE);
  CHECK_EQ(r.hops, 0);
  std::vector<uint8_t> many(64, 0x55);
  RouteResolver(d).resolve(takenRoute(many, 40), endpoint("Bob"), endpoint("Me"), r);
  CHECK_EQ(r.hops, 40);
  CHECK_EQ(r.notHeld, 40 - MSG_PATH_MAX);
  CHECK_EQ(r.count, ResolvedRoute::MAX_POINTS);
  CHECK_EQ(r.unknown, MSG_PATH_MAX);
}

// ============================================================ Map geometry (MAP-6)

TEST(zoom_range_around_the_levels_on_the_card) {
  TileGrid g(240, 111);
  CHECK_EQ(g.maxZoom(), TileGrid::NO_TILES_MAX_ZOOM);    // no card: markers on the grid
  CHECK(!g.hasDataAt(14));
  g.setLevels(12, 15);
  CHECK_EQ(g.maxZoom(), 17);
  CHECK_EQ(g.clampZoom(99), 17);
  CHECK_EQ(g.clampZoom(-4), TileGrid::MIN_ZOOM);
  CHECK(!g.hasDataAt(9));
  CHECK(g.hasDataAt(10));
  CHECK(g.hasDataAt(17));
  CHECK_EQ(g.shiftAt(13), 0);
  CHECK_EQ(g.shiftAt(16), 1);
  CHECK_EQ(g.shiftAt(17), 2);
  CHECK_EQ(g.shiftAt(11), -1);
  CHECK_EQ(g.shiftAt(10), -2);
  CHECK_EQ(g.shiftAt(9), 0);
  g.setNoLevels();
  CHECK_EQ(g.maxZoom(), TileGrid::NO_TILES_MAX_ZOOM);
}

// Every pixel of the screen belongs to exactly one piece, at any position and scale
static void checkCoverage(TileGrid& g, double left, double top, int z) {
  static int seen[111][240];
  memset(seen, 0, sizeof(seen));
  g.begin(left, top, z);
  TileBlit b;
  int pieces = 0;
  while (g.next(b)) {
    CHECK(++pieces < 100);
    for (int y = b.screenY; y < b.screenY + b.h; y++)
      for (int x = b.screenX; x < b.screenX + b.w; x++) {
        CHECK(x >= 0 && x < 240 && y >= 0 && y < 111);
        seen[y][x]++;
      }
  }
  for (auto& row : seen) for (int v : row) if (v != 1) { CHECK_EQ(v, 1); return; }
}

TEST(tile_pieces_cover_the_screen_once) {
  TileGrid g(240, 111);
  g.setLevels(12, 15);
  const double N[][2] = {{0, 0}, {7.5, 3.25}, {255.9, 255.9}, {-100.2, -30.1}, {64, 64}, {1000000.3, 999999.7}};
  for (auto& p : N) for (int z : {9, 10, 11, 12, 15, 16, 17}) checkCoverage(g, p[0], p[1], z);
}

TEST(tile_pieces_at_each_scale) {
  TileGrid g(240, 111);
  g.setLevels(12, 15);
  TileBlit b;
  // native: global pixel 2560 is the start of tile 10
  g.begin(2560, 2560, 14);
  CHECK(g.next(b));
  CHECK(b.hasData && b.z == 14 && b.x == 10 && b.y == 10 && b.shift == 0 && b.w == 240 && b.h == 111);
  // enlarged twice from level 15: tile 5 of level 15 starts at pixel 5 * 1024 of level 17
  g.begin(5 * 1024 + 100, 7 * 1024, 17);
  CHECK(g.next(b));
  CHECK(b.z == 15 && b.x == 5 && b.y == 7 && b.shift == 2 && b.localX == 100 && b.localY == 0);
  // reduced twice from level 12: each tile is 64 pixels wide on the screen
  g.begin(64 * 3, 64 * 2, 10);
  CHECK(g.next(b));
  CHECK(b.z == 12 && b.x == 3 && b.y == 2 && b.shift == -2 && b.w == 64 && b.h == 64);
  // no data: the same walk, in plain tiles of that zoom
  g.begin(0, 0, 9);
  CHECK(g.next(b));
  CHECK(!b.hasData && b.w == 240 && b.h == 111);
}

TEST(tile_pieces_at_the_edges_of_the_world) {
  TileGrid g(240, 111);
  g.setLevels(3, 5);
  TileBlit b;
  g.begin(-100, -50, 3);
  bool wrapped = false, above = false;
  while (g.next(b)) {
    if (b.x == 7) wrapped = true;        // left of tile 0: the world wraps
    if (b.y < 0) { above = true; CHECK(!b.hasData); }
    CHECK(b.x >= 0 && b.x < 8);
  }
  CHECK(wrapped && above);
  g.begin(0, 8 * 256 - 10, 3);
  bool below = false;
  while (g.next(b)) if (b.y >= 8) { below = true; CHECK(!b.hasData); }
  CHECK(below);
}

// A tile whose pixels say where they are: value = column + 256 * (row & 0xFF)
class CountingTiles : public TileSource {
public:
  bool available() override { return true; }
  bool zoomRange(int&, int&) override { return true; }
  bool hint(double&, double&) override { return false; }
  bool beginTile(int z, int x, int y) override { opened++; open = !missing; return open; }
  bool readRow(int row, int x0, int w, uint16_t* dst) override {
    reads++;
    CHECK(open && row >= 0 && row < 256 && x0 >= 0 && x0 + w <= 256 && w > 0);
    if (failAt >= 0 && reads > failAt) return false;
    for (int i = 0; i < w; i++) dst[i] = (uint16_t)((x0 + i) + 256 * row);
    return true;
  }
  void endTile() override { closed++; open = false; }
  bool missing = false, open = false;
  int opened = 0, closed = 0, reads = 0, failAt = -1;
};

class CapturedRows : public RowSink {
public:
  void row(int x, int y, int w, const uint16_t* p) override {
    for (int i = 0; i < w; i++) px[y][x + i] = p[i];
    rows++;
  }
  uint16_t px[256][256] = {};
  int rows = 0;
};

static TileBlit wholePiece(int shift, int w, int h, int localX, int localY) {
  TileBlit b;
  b.hasData = true;
  b.shift = shift;
  b.w = w;
  b.h = h;
  b.localX = localX;
  b.localY = localY;
  return b;
}

TEST(rasterizer_native_scale) {
  CountingTiles t;
  TileRasterizer r;
  CapturedRows out;
  CHECK(r.draw(t, wholePiece(0, 100, 50, 10, 20), out));
  CHECK_EQ(out.px[0][0], 10 + 256 * 20);
  CHECK_EQ(out.px[49][99], 109 + 256 * 69);
  CHECK_EQ(t.reads, 50);
  CHECK_EQ(t.closed, 1);
}

TEST(rasterizer_enlarges_and_reads_each_row_once) {
  CountingTiles t;
  TileRasterizer r;
  CapturedRows out;
  CHECK(r.draw(t, wholePiece(2, 100, 40, 6, 0), out));    // local 6 = tile pixel 1, shown 4 times
  CHECK_EQ(out.px[0][0], 1);
  CHECK_EQ(out.px[0][1], 1);
  CHECK_EQ(out.px[0][2], 2);       // local 8
  CHECK_EQ(out.px[3][0], 1);
  CHECK_EQ(out.px[4][0], 1 + 256);
  CHECK_EQ(out.rows, 40);
  CHECK_EQ(t.reads, 10);           // 40 rows on the screen, 10 on the card
}

TEST(rasterizer_reduces) {
  CountingTiles t;
  TileRasterizer r;
  CapturedRows out;
  CHECK(r.draw(t, wholePiece(-2, 64, 64, 0, 0), out));
  CHECK_EQ(out.px[0][1], 4);
  CHECK_EQ(out.px[1][0], 4 * 256);
  CHECK_EQ(out.px[63][63], 252 + 256 * 252);
  CHECK_EQ(t.reads, 64);
}

TEST(rasterizer_missing_tile_and_read_error) {
  TileRasterizer r;
  CapturedRows out;
  CountingTiles none;
  none.missing = true;
  CHECK(!r.draw(none, wholePiece(0, 10, 10, 0, 0), out));
  CHECK_EQ(out.rows, 0);
  TileBlit noData = wholePiece(0, 10, 10, 0, 0);
  noData.hasData = false;
  CountingTiles t;
  CHECK(!r.draw(t, noData, out));
  CHECK_EQ(t.opened, 0);           // nothing touched the card
  CountingTiles bad;
  bad.failAt = 3;
  CHECK(!r.draw(bad, wholePiece(0, 10, 10, 0, 0), out));
  CHECK_EQ(bad.closed, 1);         // the file is closed even on error
}

TEST(projection_clips_lines) {
  MapProjection p(10, 0, 0, 240, 111);
  double x0 = 10, y0 = 10, x1 = 100, y1 = 50;
  CHECK(p.clip(x0, y0, x1, y1));   // inside: unchanged
  CHECK(x0 == 10 && y1 == 50);
  x0 = -100; y0 = 50; x1 = 340; y1 = 50;
  CHECK(p.clip(x0, y0, x1, y1));   // crosses the view
  CHECK(x0 == 0 && x1 == 239 && y0 == 50);
  x0 = -10; y0 = -10; x1 = -5; y1 = 500;
  CHECK(!p.clip(x0, y0, x1, y1));  // stays on one side
  x0 = -1e7; y0 = -1e7; x1 = 1e7; y1 = 1e7;   // far beyond any 16-bit coordinate
  CHECK(p.clip(x0, y0, x1, y1));
  CHECK(x0 >= 0 && y0 >= 0 && x1 <= 239 && y1 <= 110);
  x0 = 300; y0 = -100; x1 = 400; y1 = -50;
  CHECK(!p.clip(x0, y0, x1, y1));
  x0 = 500; y0 = 500; x1 = 500; y1 = 500;     // one point, outside
  CHECK(!p.clip(x0, y0, x1, y1));
}

TEST(projection_places_a_position) {
  double px, py;
  geo::toPixel(43.6, 1.44, 12, px, py);
  MapProjection p(12, px - 120, py - 55, 240, 111);
  double x, y;
  p.toView(43.6, 1.44, x, y);
  CHECK(fabs(x - 120) < 1e-6 && fabs(y - 55) < 1e-6);
}

TEST(bounds_fit_a_route) {
  geo::Bounds b;
  CHECK(b.empty());
  b.add(43.60, 1.44);
  CHECK(!b.empty());
  CHECK_EQ(b.fitZoom(240, 111, 10, 3, 17), 17);        // one point: as close as allowed
  b.add(43.70, 1.34);
  b.add(43.65, 1.40);                                  // inside: changes nothing
  int z = b.fitZoom(240, 111, 10, 3, 17);
  double x0, y0, x1, y1;
  geo::toPixel(43.70, 1.34, z, x0, y0);
  geo::toPixel(43.60, 1.44, z, x1, y1);
  CHECK(x1 - x0 <= 220 && y1 - y0 <= 91);              // fits...
  geo::toPixel(43.70, 1.34, z + 1, x0, y0);
  geo::toPixel(43.60, 1.44, z + 1, x1, y1);
  CHECK(x1 - x0 > 220 || y1 - y0 > 91);                // ...and not one level closer
  double lat, lon;
  b.center(lat, lon);
  CHECK(fabs(lon - 1.39) < 1e-6);
  CHECK(lat > 43.64 && lat < 43.66);
  geo::Bounds world;
  world.add(-70, -170);
  world.add(70, 170);
  CHECK_EQ(world.fitZoom(240, 111, 10, 3, 17), 3);     // never below the minimum
  geo::Bounds both, none;
  both.add(b);
  both.add(none);
  both.add(10, 10);
  CHECK_EQ(both.fitZoom(240, 111, 10, 3, 17), 3);
}

// ============================================================ HeadingFilter (VALIDATION-10)

TEST(heading_needs_real_movement) {
  HeadingFilter h;
  h.update(true, 0.0f, 270.0f, 0);
  CHECK(!h.headingUp());
  CHECK_EQ(h.heading(), 0);
  h.update(true, 3.5f, 270.0f, 1000);       // GPS noise at a standstill: not enough
  CHECK(!h.headingUp());
  h.update(true, 4.5f, 90.0f, 2000);
  CHECK(h.headingUp());
  CHECK_EQ((int)h.heading(), 90);
}

TEST(heading_does_not_flicker_around_the_threshold) {
  HeadingFilter h;
  h.update(true, 5.0f, 90.0f, 0);
  CHECK(h.headingUp());
  for (int i = 1; i <= 20; i++) {           // walking slowly, 2.5 to 4.5 km/h
    h.update(true, i % 2 ? 2.5f : 4.5f, 90.0f, i * 1000);
    CHECK(h.headingUp());
  }
  h.update(true, 1.0f, 200.0f, 30000);      // a stop: kept for a while...
  CHECK(h.headingUp());
  CHECK_EQ((int)h.heading(), 90);           // ...without taking the meaningless course
  h.update(true, 1.0f, 200.0f, 34000);
  CHECK(h.headingUp());
  h.update(true, 1.0f, 200.0f, 35000);      // ...then north up
  CHECK(!h.headingUp());
  h.update(true, 3.0f, 10.0f, 36000);
  CHECK(!h.headingUp());
}

TEST(heading_a_short_slowdown_is_forgotten) {
  HeadingFilter h;
  h.update(true, 6.0f, 45.0f, 0);
  h.update(true, 1.0f, 45.0f, 1000);
  h.update(true, 1.0f, 45.0f, 4000);
  h.update(true, 6.0f, 45.0f, 5000);        // moving again before the 5 s
  h.update(true, 1.0f, 45.0f, 8000);
  h.update(true, 1.0f, 45.0f, 12000);
  CHECK(h.headingUp());                     // the slow time started again at 8 s
  h.update(true, 1.0f, 45.0f, 13000);
  CHECK(!h.headingUp());
}

TEST(heading_is_smoothed_on_the_circle) {
  HeadingFilter h;
  h.update(true, 10.0f, 350.0f, 0);
  for (int i = 1; i < 30; i++) h.update(true, 10.0f, 10.0f, i * 1000);
  const float a = h.heading();
  CHECK(a > 8.0f && a < 10.5f);             // went through north, not through 180
  HeadingFilter j;
  j.update(true, 10.0f, 100.0f, 0);
  j.update(true, 10.0f, 300.0f, 1000);      // one wild reading does not swing the dial
  CHECK(j.heading() > 20.0f && j.heading() < 100.0f);
  j.update(true, 10.0f, -30.0f, 2000);      // odd values from the receiver
  CHECK(j.heading() >= 0 && j.heading() < 360);
}

TEST(heading_without_fix) {
  HeadingFilter h;
  h.update(true, 8.0f, 120.0f, 0);
  CHECK(h.headingUp());
  h.update(false, 8.0f, 120.0f, 500);
  CHECK(!h.headingUp());
  CHECK_EQ(h.heading(), 0);
  h.update(true, 8.0f, 300.0f, 1000);      // a new start, not an average with the old course
  CHECK_EQ((int)h.heading(), 300);
}

// ============================================================ GeoText, ContactPosition (MESSAGING-11)

static bool geoIs(const char* text, double lat, double lon) {
  double a = 0, b = 0;
  if (!geotext::find(text, a, b)) { printf("    no position in '%s'\n", text); return false; }
  bool ok = fabs(a - lat) < 1e-6 && fabs(b - lon) < 1e-6;
  if (!ok) printf("    '%s' -> %f, %f\n", text, a, b);
  return ok;
}

static bool geoNone(const char* text) {
  double a, b;
  bool found = geotext::find(text, a, b);
  if (found) printf("    unexpected position in '%s': %f, %f\n", text, a, b);
  return !found;
}

TEST(geotext_reads_the_usual_forms) {
  CHECK(geoIs("geo:43.6047,1.4442", 43.6047, 1.4442));
  CHECK(geoIs("I am here geo:43.604700,1.444200;u=10", 43.6047, 1.4442));
  CHECK(geoIs("43.6047, 1.4442", 43.6047, 1.4442));
  CHECK(geoIs("meet at -33.8688,151.2093 ok?", -33.8688, 151.2093));
  CHECK(geoIs("https://maps.google.com/?q=43.6047,1.4442", 43.6047, 1.4442));
  CHECK(geoIs("https://www.google.com/maps/place/x/@48.8584,2.2945,17z", 48.8584, 2.2945));
  CHECK(geoIs("https://maps.apple.com/?ll=45.764,4.8357", 45.764, 4.8357));
  CHECK(geoIs("https://www.openstreetmap.org/?mlat=43.60470&mlon=1.44420#map=17", 43.6047, 1.4442));
  CHECK(geoIs("geo:-12,130", -12, 130));        // geo: needs no decimals
}

TEST(geotext_ignores_what_is_not_a_place) {
  CHECK(geoNone(""));
  CHECK(geoNone(nullptr));
  CHECK(geoNone("see you at 18h"));
  CHECK(geoNone("1.5, 2.5 km"));                // a bare pair needs precise numbers
  CHECK(geoNone("SNR 8.25, 9.75 dB"));
  CHECK(geoNone("version 1.14.2, 3.0.1"));
  CHECK(geoNone("95.1234, 10.1234"));           // latitude out of range
  CHECK(geoNone("45.1234, 190.1234"));          // longitude out of range
  CHECK(geoNone("geo:0,0"));                    // nowhere
  CHECK(geoNone("geo:abc"));
  CHECK(geoNone("v2.1234,3.4567"));             // inside a word
  CHECK(geoNone("12.34567,8.91011.5"));         // not a clean pair
}

TEST(geotext_writes_what_it_reads) {
  char out[48];
  geotext::format(43.604712, -1.444234, out, sizeof(out));
  CHECK(strcmp(out, "geo:43.604712,-1.444234") == 0);
  CHECK(geoIs(out, 43.604712, -1.444234));
}

TEST(contact_position_keeps_the_newest) {
  ContactPosition p;
  CHECK(!p.known());
  CHECK(p.offer(43600000, 1440000, 1000, PositionSource::Advert));
  CHECK(p.offer(43610000, 1450000, 2000, PositionSource::Message));
  CHECK(p.source == PositionSource::Message && p.latE6 == 43610000);
  CHECK(!p.offer(43000000, 1000000, 1500, PositionSource::Advert));   // an older advert arriving late
  CHECK_EQ(p.latE6, 43610000);
  CHECK(p.offer(43620000, 1460000, 2000, PositionSource::Advert));    // the same time: the latest heard
  CHECK(!p.offer(0, 0, 9999, PositionSource::Message));               // no position
  ContactPosition q;
  CHECK(q.offer(1, 2, 0, PositionSource::Advert));                    // unknown time, nothing held: taken
  CHECK(q.offer(3, 4, 50, PositionSource::Message));
  CHECK(!q.offer(5, 6, 0, PositionSource::Advert));                   // unknown time never replaces a known one
}

// ============================================================ PositionBook (MESSAGING-11)

TEST(position_book_by_key) {
  static PositionBook b;
  b.clear();
  uint8_t k1[6] = {1, 2, 3, 4, 5, 6}, k2[6] = {1, 2, 3, 4, 5, 7};
  CHECK(b.get(k1).source == PositionSource::None);
  PositionBook::Meta m;
  m.time = 1000;
  m.source = PositionSource::Message;
  CHECK(b.set(k1, m));
  CHECK(b.get(k1).time == 1000 && b.get(k1).source == PositionSource::Message);
  CHECK(b.get(k2).time == 0);              // one different byte: another contact
  m.time = 2000;
  CHECK(b.set(k1, m));
  CHECK_EQ(b.count(), 1);
  CHECK_EQ(b.get(k1).time, 2000u);
  b.remove(k1);
  CHECK_EQ(b.count(), 0);
  for (int i = 0; i < PositionBook::CAPACITY; i++) {
    uint8_t k[6] = {(uint8_t)i, (uint8_t)(i >> 8), 9, 9, 9, 9};
    CHECK(b.set(k, m));
  }
  CHECK(!b.set(k2, m));                    // full: refused, nothing overwritten
  uint8_t first[6] = {0, 0, 9, 9, 9, 9};
  CHECK_EQ(b.get(first).time, 2000u);
}

// ============================================================ ContactCapacity (VALIDATION-11)

TEST(contact_capacity_warns_once_per_level) {
  using A = ContactCapacity::Alert;
  ContactCapacity c;
  CHECK(c.poll(50, 100) == A::None);
  CHECK(c.poll(89, 100) == A::None);
  CHECK(c.poll(90, 100) == A::Near);
  CHECK(c.poll(95, 100) == A::None);          // told once
  CHECK(c.poll(100, 100) == A::Full);
  CHECK(c.poll(100, 100) == A::None);         // replacing goes on, quietly
  CHECK(c.poll(99, 100) == A::None);          // one removed...
  CHECK(c.poll(100, 100) == A::Full);         // ...full again: said again
  CHECK(c.poll(86, 100) == A::None);
  CHECK(c.poll(90, 100) == A::None);          // not below 85 % in between: no new warning
  CHECK(c.poll(80, 100) == A::None);
  CHECK(c.poll(91, 100) == A::Near);
  CHECK(c.poll(5, 0) == A::None);             // no limit known
  CHECK(ContactCapacity::near(9, 10));
  CHECK(!ContactCapacity::near(8, 10));
}

// ============================================================ main

// ============================================================ BucketStore (MESSAGING-12, step 1)

namespace bucket_test {
constexpr uint16_t REC = 96, MAXREC = 42;

BucketStore::Layout layout(uint16_t version = 1) {
  BucketStore::Layout l;
  l.prefix = "/nb";
  l.magic = 0x3142444e;   // "NDB1"
  l.version = version;
  l.recordSize = REC;
  l.buckets = 128;
  l.maxRecords = MAXREC;
  return l;
}

// `n` records whose bytes all say `tag` and their index: two contents never look alike
std::vector<uint8_t> records(uint8_t tag, int n) {
  std::vector<uint8_t> v((size_t)REC * n);
  for (int i = 0; i < n; i++) memset(v.data() + (size_t)i * REC, tag ^ (uint8_t)(i * 7), REC);
  return v;
}

bool holds(BucketStore& st, uint16_t b, const std::vector<uint8_t>& want, BucketStore::Status* status = nullptr) {
  std::vector<uint8_t> buf(st.bufferBytes(), 0xEE);
  uint16_t count = 0xFFFF;
  BucketStore::Status s = st.load(b, buf.data(), count);
  if (status) *status = s;
  return (size_t)count * REC == want.size() && memcmp(buf.data(), want.data(), want.size()) == 0;
}
}  // namespace bucket_test

TEST(bucket_store_crc_is_the_zlib_one) {
  CHECK_EQ(BucketStore::crc32(0, "123456789", 9), 0xCBF43926u);
  CHECK_EQ(BucketStore::crc32(BucketStore::crc32(0, "1234", 4), "56789", 5), 0xCBF43926u);   // in pieces
}

TEST(bucket_store_a_full_bucket_fits_one_flash_block) {
  FakeStorage disk;
  BucketStore st(disk, bucket_test::layout());
  CHECK(st.valid());
  CHECK(st.fileBytes(bucket_test::MAXREC) <= 4096);
  CHECK_EQ(st.bufferBytes(), 96u * 42u);
}

TEST(bucket_store_round_trip) {
  using namespace bucket_test;
  FakeStorage disk;
  BucketStore st(disk, layout());
  std::vector<uint8_t> none;
  BucketStore::Status s;
  CHECK(holds(st, 5, none, &s));
  CHECK(s == BucketStore::Status::Empty);

  const std::vector<uint8_t> a = records(0x11, 3), b = records(0x22, MAXREC);
  CHECK(st.save(5, a.data(), 3));
  CHECK(st.save(0x7f, b.data(), MAXREC));
  CHECK(disk.exists("/nb05"));
  CHECK(disk.exists("/nb7f"));
  CHECK(!disk.exists("/nb05.new"));
  CHECK_EQ(disk.files["/nb05"].size(), st.fileBytes(3));
  CHECK(holds(st, 5, a, &s));
  CHECK(s == BucketStore::Status::Ok);
  CHECK(holds(st, 0x7f, b));

  BucketStore again(disk, layout());   // after a restart
  CHECK(holds(again, 5, a));

  const std::vector<uint8_t> shorter = records(0x33, 1);   // a smaller file replaces a bigger one whole
  CHECK(st.save(5, shorter.data(), 1));
  CHECK(holds(st, 5, shorter));
  CHECK_EQ(disk.files["/nb05"].size(), st.fileBytes(1));

  CHECK(st.save(5, nullptr, 0));       // emptied: no file left
  CHECK(!disk.exists("/nb05"));
  CHECK(holds(st, 5, none, &s));
  CHECK(s == BucketStore::Status::Empty);
}

TEST(bucket_store_refuses_what_cannot_fit) {
  using namespace bucket_test;
  FakeStorage disk;
  BucketStore st(disk, layout());
  const std::vector<uint8_t> big = records(1, MAXREC + 1);
  g_coreLogCapture.clear();
  CHECK(!st.save(3, big.data(), MAXREC + 1));
  CHECK(!st.save(128, big.data(), 1));          // no bucket 128
  CHECK(logHas("save of bucket 3 refused"));
  CHECK(disk.files.empty());
  uint16_t count = 9;
  std::vector<uint8_t> buf(st.bufferBytes());
  CHECK(st.load(128, buf.data(), count) == BucketStore::Status::Failed);
  CHECK_EQ(count, 0);

  BucketStore::Layout l = layout();
  l.recordSize = 0;
  BucketStore broken(disk, l);
  CHECK(!broken.valid());
  CHECK(logHas("unusable layout"));
  CHECK(broken.load(0, buf.data(), count) == BucketStore::Status::Failed);
  CHECK(!broken.save(0, buf.data(), 1));
}

// The power goes at every step of a save, with a write cut at several lengths: after the restart the bucket holds the
// old content or the new one, never something else, and the next save works and leaves no temporary file.
static void cutEverywhere(bool withOld, bool atomic = false) {
  using namespace bucket_test;
  const std::vector<uint8_t> oldRecs = withOld ? records(0xA0, 5) : std::vector<uint8_t>();
  const std::vector<uint8_t> newRecs = records(0x5B, 9), later = records(0xC3, 2);

  uint32_t steps;   // changes of a save that is not cut
  {
    FakeStorage disk;
    disk.setAtomic(atomic);
    BucketStore st(disk, layout());
    if (withOld) CHECK(st.save(9, oldRecs.data(), 5));
    const uint32_t before = disk.changes();
    CHECK(st.save(9, newRecs.data(), 9));
    steps = disk.changes() - before;
  }
  CHECK(atomic ? steps == 1 : steps >= 4);   // LittleFS: one write; otherwise ".new", header, records, remove, rename

  int sawOld = 0, sawNew = 0;
  const uint32_t keeps[] = {0, 1, 15, 16, 17, 200, 100000};
  for (uint32_t step = 0; step < steps; step++) {
    for (uint32_t keep : keeps) {
      FakeStorage disk;
      disk.setAtomic(atomic);
      BucketStore st(disk, layout());
      if (withOld) CHECK(st.save(9, oldRecs.data(), 5));
      disk.cutAt(step, keep);
      CHECK(!st.save(9, newRecs.data(), 9));
      CHECK(disk.powerLost());
      disk.reboot();

      BucketStore afterCut(disk, layout());
      BucketStore::Status s;
      const bool isOld = holds(afterCut, 9, oldRecs, &s);
      const bool isNew = !isOld && holds(afterCut, 9, newRecs, &s);
      if (!isOld && !isNew) printf("    cut at step %u keeping %u bytes: neither old nor new\n", step, keep);
      CHECK(isOld || isNew);
      CHECK(s != BucketStore::Status::SetAside && s != BucketStore::Status::Failed);
      sawOld += isOld;
      sawNew += isNew;

      CHECK(afterCut.save(9, later.data(), 2));
      CHECK(holds(afterCut, 9, later));
      CHECK(!disk.exists("/nb09.new"));
      CHECK(!disk.exists("/nb09.bad"));
    }
  }
  CHECK(sawOld > 0);
  if (!atomic) CHECK(sawNew > 0);   // the cut between the remove and the rename: load() finishes the save
}

TEST(bucket_store_survives_a_power_cut_at_every_step) {
  cutEverywhere(true);
  cutEverywhere(false);
  cutEverywhere(true, true);           // LittleFS: one atomic write per save
  cutEverywhere(false, true);
}

TEST(bucket_store_on_an_atomic_file_system_makes_one_write_per_save) {
  using namespace bucket_test;
  FakeStorage disk;
  disk.setAtomic(true);
  BucketStore st(disk, layout());
  const std::vector<uint8_t> a = records(0x10, 5), b = records(0x11, 2);
  CHECK(st.save(1, a.data(), 5));
  const uint32_t before = disk.changes(), reads = disk.reads;
  CHECK(st.save(1, b.data(), 2));
  CHECK_EQ(disk.changes() - before, 1u);   // no ".new", no remove, no rename
  CHECK(holds(st, 1, b));
  std::vector<uint8_t> none;
  CHECK(holds(st, 2, none));
  CHECK(disk.reads - reads <= 4u);        // an empty bucket: no look for a ".new"
  CHECK(!disk.exists("/nb01.new"));
}

TEST(bucket_store_spreads_buckets_over_directories) {
  using namespace bucket_test;
  FakeStorage disk;
  disk.setAtomic(true);
  disk.setRequireDirs(true);             // like LittleFS: no file without its directory
  BucketStore::Layout l = layout();
  l.prefix = "/nodes";
  l.dirs = 8;
  BucketStore st(disk, l);
  CHECK(st.valid());
  const std::vector<uint8_t> a = records(0x20, 3);
  CHECK(st.save(0, a.data(), 3));
  CHECK(st.save(0x7f, a.data(), 3));
  CHECK(st.save(0x10, a.data(), 3));
  CHECK(disk.exists("/nodes/0/00"));
  CHECK(disk.exists("/nodes/7/7f"));
  CHECK(disk.exists("/nodes/1/10"));     // 16 buckets per directory
  CHECK_EQ(disk.dirs.size(), 4u);        // /nodes, /nodes/0, /nodes/1, /nodes/7
  CHECK(holds(st, 0x7f, a));
  BucketStore again(disk, l);            // after a restart the directories exist: saving still works
  CHECK(again.save(0x11, a.data(), 3));
  CHECK(disk.exists("/nodes/1/11"));

  FakeStorage noDirs;                    // a directory that cannot be made is reported
  noDirs.setRequireDirs(true);
  BucketStore broken(noDirs, l);
  noDirs.failWrites(false);
  noDirs.cutAt(0);                       // the power goes at the first mkdir
  g_coreLogCapture.clear();
  CHECK(!broken.save(3, a.data(), 3));
  CHECK(logHas("mkdir /nodes failed"));
}

TEST(bucket_store_finishes_a_save_cut_before_its_rename) {
  using namespace bucket_test;
  FakeStorage disk;
  BucketStore st(disk, layout());
  const std::vector<uint8_t> a = records(1, 2), b = records(2, 4);
  CHECK(st.save(1, a.data(), 2));
  const uint32_t before = disk.changes();
  CHECK(st.save(1, b.data(), 4));
  const uint32_t steps = disk.changes() - before;

  FakeStorage cut;
  BucketStore st2(cut, layout());
  CHECK(st2.save(1, a.data(), 2));
  cut.cutAt(steps - 1);                // the rename
  CHECK(!st2.save(1, b.data(), 4));
  cut.reboot();
  CHECK(!cut.exists("/nb01"));
  CHECK(cut.exists("/nb01.new"));
  g_coreLogCapture.clear();
  BucketStore::Status s;
  CHECK(holds(st2, 1, b, &s));
  CHECK(s == BucketStore::Status::Recovered);
  CHECK(logHas("finished a save cut by a restart (4 records)"));
  CHECK(cut.exists("/nb01"));
  CHECK(!cut.exists("/nb01.new"));
  CHECK_EQ(st2.stats().recovered, 1u);
}

TEST(bucket_store_sets_a_damaged_file_aside) {
  using namespace bucket_test;
  FakeStorage disk;
  BucketStore st(disk, layout());
  const std::vector<uint8_t> a = records(0x40, 4), b = records(0x41, 2);
  CHECK(st.save(2, a.data(), 4));
  CHECK(st.save(3, b.data(), 2));
  disk.files["/nb02"][BucketStore::HEADER_BYTES + 100] ^= 0x01;   // one bit flipped in a record

  g_coreLogCapture.clear();
  std::vector<uint8_t> none;
  BucketStore::Status s;
  CHECK(holds(st, 2, none, &s));
  CHECK(s == BucketStore::Status::SetAside);
  CHECK(logHas("/nb02: CRC"));
  CHECK(logHas("kept as .bad, bucket 2 starts empty"));
  CHECK(!disk.exists("/nb02"));
  CHECK(disk.exists("/nb02.bad"));
  CHECK_EQ(disk.files["/nb02.bad"].size(), st.fileBytes(4));   // the evidence, untouched
  CHECK(holds(st, 3, b));              // the other buckets are not affected
  CHECK_EQ(st.stats().setAside, 1u);

  CHECK(st.save(2, b.data(), 2));      // the bucket works again
  CHECK(holds(st, 2, b));
}

TEST(bucket_store_sets_aside_what_it_cannot_read) {
  using namespace bucket_test;
  const std::vector<uint8_t> a = records(0x50, 3);
  std::vector<uint8_t> none;
  BucketStore::Status s;
  {   // another version of the record format: never read as this one
    FakeStorage disk;
    BucketStore v1(disk, layout(1));
    CHECK(v1.save(4, a.data(), 3));
    BucketStore v2(disk, layout(2));
    g_coreLogCapture.clear();
    CHECK(holds(v2, 4, none, &s));
    CHECK(s == BucketStore::Status::SetAside);
    CHECK(logHas("format version 1 with 96-byte records, this firmware reads version 2"));
    CHECK(disk.exists("/nb04.bad"));
  }
  {   // a file copied under another bucket's name
    FakeStorage disk;
    BucketStore st(disk, layout());
    CHECK(st.save(4, a.data(), 3));
    disk.files["/nb06"] = disk.files["/nb04"];
    g_coreLogCapture.clear();
    CHECK(holds(st, 6, none, &s));
    CHECK(s == BucketStore::Status::SetAside);
    CHECK(logHas("header says bucket 4 with 3 records (bucket 6"));
  }
  {   // shorter than its header, or than its records
    FakeStorage disk;
    BucketStore st(disk, layout());
    disk.files["/nb07"] = std::vector<uint8_t>(10, 0);
    g_coreLogCapture.clear();
    CHECK(holds(st, 7, none, &s));
    CHECK(s == BucketStore::Status::SetAside);
    CHECK(logHas("shorter than its header"));
    CHECK(st.save(8, a.data(), 3));
    disk.files["/nb08"].resize(st.fileBytes(2));
    CHECK(holds(st, 8, none, &s));
    CHECK(s == BucketStore::Status::SetAside);
    CHECK(logHas("cut short: 3 records announced"));
  }
  {   // not a bucket at all
    FakeStorage disk;
    BucketStore st(disk, layout());
    disk.files["/nb09"] = std::vector<uint8_t>(64, 0x5A);
    g_coreLogCapture.clear();
    CHECK(holds(st, 9, none, &s));
    CHECK(s == BucketStore::Status::SetAside);
    CHECK(logHas("magic 5a5a5a5a, expected 3142444e"));
  }
}

TEST(bucket_store_keeps_the_old_content_when_writes_fail) {
  using namespace bucket_test;
  FakeStorage disk;
  BucketStore st(disk, layout());
  const std::vector<uint8_t> a = records(0x60, 3), b = records(0x61, 5);
  CHECK(st.save(10, a.data(), 3));
  disk.failWrites(true);               // the partition is full
  g_coreLogCapture.clear();
  CHECK(!st.save(10, b.data(), 5));
  CHECK(logHas("write /nb0a.new failed (offset 0, 496 bytes)"));
  CHECK_EQ(st.stats().failures, 1u);
  disk.failWrites(false);
  CHECK(holds(st, 10, a));
}

TEST(bucket_store_ignores_an_unfinished_save_beside_a_good_file) {
  using namespace bucket_test;
  FakeStorage disk;
  BucketStore st(disk, layout());
  const std::vector<uint8_t> a = records(0x70, 2), b = records(0x71, 1);
  CHECK(st.save(11, a.data(), 2));
  disk.files["/nb0b.new"] = std::vector<uint8_t>(5000, 0x99);   // longer than any bucket
  BucketStore::Status s;
  CHECK(holds(st, 11, a, &s));
  CHECK(s == BucketStore::Status::Ok);
  CHECK(st.save(11, b.data(), 1));
  CHECK(holds(st, 11, b));
  CHECK(!disk.exists("/nb0b.new"));
  CHECK_EQ(disk.files["/nb0b"].size(), st.fileBytes(1));     // nothing of the long leftover
}

TEST(bucket_store_removes_a_save_cut_before_its_records) {
  using namespace bucket_test;
  FakeStorage disk;
  BucketStore st(disk, layout());
  const std::vector<uint8_t> a = records(0x80, 6);
  disk.cutAt(1, 200);                  // the header is written, the records never
  CHECK(!st.save(12, a.data(), 6));
  disk.reboot();
  CHECK(disk.exists("/nb0c.new"));
  g_coreLogCapture.clear();
  std::vector<uint8_t> none;
  BucketStore::Status s;
  CHECK(holds(st, 12, none, &s));
  CHECK(s == BucketStore::Status::Empty);
  CHECK(logHas("/nb0c.new: removed an unfinished save (cut short: 6 records announced)"));
  CHECK(!disk.exists("/nb0c.new"));
  CHECK_EQ(st.stats().discarded, 1u);
}

// ============================================================ NodeRecord, NodeArchive (MESSAGING-12, step 2)

namespace node_test {
// A key whose first byte picks the bucket (key[0] >> 1) and whose second byte tells the nodes of a test apart
NodeRecord node(uint8_t first, uint8_t second, const char* name, uint8_t type, uint32_t heard) {
  NodeRecord r;
  for (int i = 0; i < 32; i++) r.pubKey[i] = (uint8_t)(i * 13 + 1);
  r.pubKey[0] = first;
  r.pubKey[1] = second;
  snprintf(r.name, sizeof(r.name), "%s", name);
  r.type = type;
  r.lastHeard = heard;
  r.hops = 2;
  return r;
}
NodeRecord withPos(NodeRecord r, int32_t lat, int32_t lon, uint32_t when, PositionSource src) {
  r.flags |= NodeRecord::HasPos;
  r.latE6 = lat;
  r.lonE6 = lon;
  r.posTime = when;
  r.posSource = (uint8_t)src;
  return r;
}
constexpr uint32_t T0 = 1780000000;   // 2026
}  // namespace node_test

TEST(node_record_has_a_fixed_little_endian_layout) {
  NodeRecord r = node_test::node(0xA1, 0xB2, "Relais Montmartre", CONTACT_REPEATER, 0x11223344);
  r.flags = NodeRecord::Favourite | NodeRecord::HasPos;
  r.hops = 3;
  r.snrX4 = -22;
  r.rssi = NodeRecord::clampRssi(-140);
  r.posSource = 1;
  r.latE6 = 48886700;
  r.lonE6 = -2343000;
  r.posTime = 0x01020304;
  r.firstHeard = 0x0A0B0C0D;
  r.lastAdvertTs = 0xCAFEBABE;
  uint8_t b[NodeRecordCodec::BYTES];
  memset(b, 0x77, sizeof(b));
  NodeRecordCodec::encode(r, b);
  CHECK_EQ(b[0], 0xA1);
  CHECK_EQ(b[1], 0xB2);
  CHECK(memcmp(b + 32, "Relais Montmartre", 17) == 0);
  CHECK_EQ(b[32 + 17], 0);              // padded with zeros, not with what was in the buffer
  CHECK_EQ(b[63], 0);
  CHECK_EQ(b[64], CONTACT_REPEATER);
  CHECK_EQ(b[65], NodeRecord::Favourite | NodeRecord::HasPos);
  CHECK_EQ(b[66], 3);
  CHECK_EQ((int8_t)b[67], -22);
  CHECK_EQ((int8_t)b[68], -128);        // clamped
  CHECK_EQ(b[70] | b[71], 0);           // reserved
  CHECK_EQ(b[72] | b[73] << 8 | b[74] << 16 | (uint32_t)b[75] << 24, 48886700u);
  CHECK_EQ((int32_t)(b[76] | b[77] << 8 | b[78] << 16 | (uint32_t)b[79] << 24), -2343000);
  CHECK_EQ(b[80], 0x04);
  CHECK_EQ(b[83], 0x01);
  CHECK_EQ(b[84], 0x0D);
  CHECK_EQ(b[88], 0x44);
  CHECK_EQ(b[91], 0x11);
  CHECK_EQ(b[92], 0xBE);
  CHECK_EQ(b[95], 0xCA);

  NodeRecord d;
  NodeRecordCodec::decode(b, d);
  uint8_t again[NodeRecordCodec::BYTES];
  NodeRecordCodec::encode(d, again);
  CHECK(memcmp(b, again, sizeof(b)) == 0);
  CHECK(strcmp(d.name, "Relais Montmartre") == 0);
  CHECK_EQ(d.lonE6, -2343000);
  CHECK_EQ(d.lastAdvertTs, 0xCAFEBABEu);

  memset(b + 32, 'x', 32);              // a name filling the field: still terminated
  NodeRecordCodec::decode(b, d);
  CHECK_EQ(strlen(d.name), 31u);
}

TEST(node_archive_adds_finds_and_keeps_across_restarts) {
  using namespace node_test;
  FakeStorage disk;
  {
    NodeArchive a(disk);
    CHECK(a.upsert(node(0x10, 1, "Alice", CONTACT_CHAT, T0)) == NodeArchive::Result::Added);
    CHECK(a.upsert(node(0x10, 2, "Relay", CONTACT_REPEATER, T0)) == NodeArchive::Result::Added);
    CHECK(a.upsert(node(0xF0, 1, "Far", CONTACT_ROOM, T0)) == NodeArchive::Result::Added);
  }
  CHECK(disk.exists("/nb08"));          // 0x10 >> 1
  CHECK(disk.exists("/nb78"));
  NodeArchive a(disk);
  NodeRecord r;
  CHECK(a.get(node(0x10, 1, "", 0, 0).pubKey, r));
  CHECK(strcmp(r.name, "Alice") == 0);
  CHECK_EQ(r.firstHeard, T0);           // first heard = when added
  CHECK(!a.get(node(0x10, 3, "", 0, 0).pubKey, r));
  CHECK_EQ(a.knownCount(), -1);         // not every bucket read yet
}

TEST(node_archive_merges_what_is_heard_again) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive a(disk);
  const NodeRecord base = node(0x20, 1, "Bob", CONTACT_CHAT, T0);
  CHECK(a.upsert(withPos(base, 48800000, 2300000, T0 - 10, PositionSource::Message)) == NodeArchive::Result::Added);

  // An older advert arriving later: its position is older than the message's, it does not move Bob
  NodeRecord advert = withPos(node(0x20, 1, "Bob", CONTACT_CHAT, T0 + 2 * NodeArchive::REFRESH_S), 1, 1, T0 - 100,
                              PositionSource::Advert);
  advert.hops = 5;
  advert.snrX4 = -8;
  CHECK(a.upsert(advert) == NodeArchive::Result::Updated);   // updated: more than an hour later
  NodeRecord r;
  CHECK(a.get(base.pubKey, r));
  CHECK_EQ(r.latE6, 48800000);
  CHECK_EQ(r.posSource, (int)PositionSource::Message);
  CHECK_EQ(r.lastHeard, T0 + 2 * NodeArchive::REFRESH_S);
  CHECK_EQ(r.firstHeard, T0);           // kept
  CHECK_EQ(r.hops, 5);                  // the signal of the newest hearing
  CHECK_EQ(r.snrX4, -8);

  // A late packet, heard before the last one: its signal is not taken, the time does not go back
  NodeRecord late = node(0x20, 1, "Bob", CONTACT_CHAT, T0 + 10);
  late.hops = 1;
  late.name[0] = 0;                     // no name: the stored one stays
  CHECK(a.upsert(late) == NodeArchive::Result::Unchanged);
  CHECK(a.get(base.pubKey, r));
  CHECK_EQ(r.hops, 5);
  CHECK(strcmp(r.name, "Bob") == 0);

  // A new name and a newer position are written at once
  CHECK(a.upsert(node(0x20, 1, "Bob 2", CONTACT_CHAT, T0 + 2 * NodeArchive::REFRESH_S + 1)) ==
        NodeArchive::Result::Updated);
  CHECK(a.upsert(withPos(node(0x20, 1, "Bob 2", CONTACT_CHAT, T0 + 2 * NodeArchive::REFRESH_S + 2), 48900000,
                         2400000, T0 + 50, PositionSource::Advert)) == NodeArchive::Result::Updated);
  CHECK(a.get(base.pubKey, r));
  CHECK(strcmp(r.name, "Bob 2") == 0);
  CHECK_EQ(r.latE6, 48900000);
  CHECK_EQ(r.posTime, T0 + 50);
}

TEST(node_archive_does_not_write_a_node_heard_again_within_the_hour) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive a(disk);
  NodeRecord n = node(0x30, 1, "Repeater", CONTACT_REPEATER, T0);
  CHECK(a.upsert(n) == NodeArchive::Result::Added);
  const uint32_t writes = disk.writes;
  for (int i = 1; i <= 20; i++) {       // an advert every 3 minutes for an hour, through other paths
    n.lastHeard = T0 + 179 * (uint32_t)i;
    n.lastAdvertTs = n.lastHeard;
    n.hops = (uint8_t)(i % 4);
    CHECK(a.upsert(n) == NodeArchive::Result::Unchanged);
  }
  CHECK_EQ(disk.writes, writes);        // not one flash write
  n.lastHeard = T0 + NodeArchive::REFRESH_S;
  n.lastAdvertTs = n.lastHeard;
  CHECK(a.upsert(n) == NodeArchive::Result::Updated);
  CHECK(disk.writes > writes);
  NodeRecord r;
  CHECK(a.get(n.pubKey, r));
  CHECK_EQ(r.lastHeard, T0 + NodeArchive::REFRESH_S);
  CHECK_EQ(r.lastAdvertTs, T0 + NodeArchive::REFRESH_S);
}

TEST(node_archive_finds_nodes_from_the_id_of_a_hop) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive a(disk);
  // 0x40 and 0x41 share bucket 0x20; 0x40 0x07 twice (a 2-byte ID collision), then a third byte tells them apart
  NodeRecord r1 = node(0x40, 0x07, "R1", CONTACT_REPEATER, T0), r2 = node(0x40, 0x07, "R2", CONTACT_REPEATER, T0);
  r2.pubKey[2] = 0x99;
  CHECK(a.upsert(r1) == NodeArchive::Result::Added);
  CHECK(a.upsert(r2) == NodeArchive::Result::Added);
  CHECK(a.upsert(node(0x40, 0x08, "Alice", CONTACT_CHAT, T0)) == NodeArchive::Result::Added);
  CHECK(a.upsert(node(0x41, 0x07, "Other", CONTACT_REPEATER, T0)) == NodeArchive::Result::Added);

  const uint8_t one[] = {0x40}, two[] = {0x40, 0x07}, three[] = {0x40, 0x07, 0x99};
  NodeRecord out[4];
  CHECK_EQ(a.findByHash(one, 1, NodeArchive::ALL_TYPES, out, 4), 3);   // not the 0x41 one
  const uint8_t relays = NodeArchive::typeBit(CONTACT_REPEATER) | NodeArchive::typeBit(CONTACT_ROOM);
  CHECK_EQ(a.findByHash(one, 1, relays, out, 4), 2);                     // Alice does not relay
  CHECK_EQ(a.findByHash(two, 2, relays, out, 1), 2);                     // more than max: the count says so
  CHECK(strcmp(out[0].name, "R1") == 0);
  CHECK_EQ(a.findByHash(three, 3, relays, out, 4), 1);
  CHECK(strcmp(out[0].name, "R2") == 0);
  CHECK_EQ(a.findByHash(one, 0, relays, out, 4), 0);
  const uint8_t none[] = {0x42};
  CHECK_EQ(a.findByHash(none, 1, relays, out, 4), 0);
}

TEST(node_archive_drops_the_weakest_node_of_a_full_bucket) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive a(disk);
  // Bucket 0x05 (first bytes 0x0a, 0x0b) filled with 42 nodes heard at T0 + i; n2 and n3 tie on time, n3 is farther
  for (int i = 0; i < NodeArchive::PER_BUCKET; i++) {
    NodeRecord n = node((uint8_t)(0x0a + (i & 1)), (uint8_t)i, "n", CONTACT_REPEATER, T0 + (uint32_t)(i == 3 ? 2 : i));
    snprintf(n.name, sizeof(n.name), "n%d", i);
    if (i == 3) n.hops = 6;
    CHECK(a.upsert(n) == NodeArchive::Result::Added);
  }
  // The two oldest are protected: a favourite and a contact
  CHECK(a.setFlags(node(0x0a, 0, "", 0, 0).pubKey, NodeRecord::Favourite, 0));
  CHECK(a.setFlags(node(0x0b, 1, "", 0, 0).pubKey, NodeRecord::InContacts, 0));
  const NodeRecord n3 = node(0x0b, 3, "", 0, 0);

  g_coreLogCapture.clear();
  NodeRecord dropped;
  CHECK(a.upsert(node(0x0a, 100, "newcomer", CONTACT_REPEATER, T0 + 1000), &dropped) == NodeArchive::Result::Replaced);
  CHECK(strcmp(dropped.name, "n3") == 0);   // n0, n1 protected; n2 and n3 tie on time, n3 has more hops
  CHECK(logHas("bucket 05 full: 'n3'"));
  CHECK(logHas("leaves for 'newcomer'"));
  NodeRecord r;
  CHECK(!a.get(n3.pubKey, r));
  CHECK(a.get(node(0x0a, 100, "", 0, 0).pubKey, r));
  CHECK(a.get(node(0x0a, 0, "", 0, 0).pubKey, r));        // the favourite stays

  CHECK(a.upsert(node(0x0a, 101, "next", CONTACT_REPEATER, T0 + 1001), &dropped) == NodeArchive::Result::Replaced);
  CHECK(strcmp(dropped.name, "n2") == 0);

  // An import is weaker than every node heard: refused
  NodeRecord imp = node(0x0b, 102, "imported", CONTACT_REPEATER, 0);
  imp.flags = NodeRecord::Imported;
  g_coreLogCapture.clear();
  CHECK(a.upsert(imp) == NodeArchive::Result::Refused);
  CHECK(logHas("is not newer than any node in it"));
}

TEST(node_archive_drops_imported_nodes_first_and_never_protected_ones) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive a(disk);
  for (int i = 0; i < NodeArchive::PER_BUCKET; i++) {
    NodeRecord n = node(0x60, (uint8_t)i, "n", CONTACT_REPEATER, T0 + 100 + (uint32_t)i);
    if (i == 20) {
      n.flags = NodeRecord::Imported;
      n.lastHeard = 0;
      snprintf(n.name, sizeof(n.name), "imported");
    }
    CHECK(a.upsert(n) != NodeArchive::Result::Failed);
  }
  NodeRecord dropped;
  CHECK(a.upsert(node(0x60, 200, "heard", CONTACT_REPEATER, T0), &dropped) == NodeArchive::Result::Replaced);
  CHECK(strcmp(dropped.name, "imported") == 0);   // before older nodes that were heard

  for (int i = 0; i < 256; i++) a.setFlags(node(0x60, (uint8_t)i, "", 0, 0).pubKey, NodeRecord::Keep, 0);
  g_coreLogCapture.clear();
  CHECK(a.upsert(node(0x60, 201, "late", CONTACT_REPEATER, T0 + 5000)) == NodeArchive::Result::Refused);
  CHECK(logHas("bucket 30 full of protected nodes: 'late'"));

  // A node imported then heard loses its mark
  NodeRecord imp = node(0x70, 1, "Hill", CONTACT_REPEATER, 0);
  imp.flags = NodeRecord::Imported;
  CHECK(a.upsert(imp) == NodeArchive::Result::Added);
  CHECK(a.upsert(node(0x70, 1, "Hill", CONTACT_REPEATER, T0)) == NodeArchive::Result::Updated);
  NodeRecord r;
  CHECK(a.get(imp.pubKey, r));
  CHECK(!r.has(NodeRecord::Imported));
  CHECK_EQ(r.firstHeard, T0);
}

TEST(node_archive_scans_in_slices_and_removes) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive a(disk);
  for (int i = 0; i < 300; i++) {       // spread over every bucket
    NodeRecord n = node((uint8_t)(i * 37), (uint8_t)(i >> 3), "x", CONTACT_CHAT, T0);
    n.pubKey[2] = (uint8_t)i;
    CHECK(a.upsert(n) == NodeArchive::Result::Added);
  }
  struct Counter : NodeVisitor {
    int n = 0;
    bool visit(const NodeRecord&) override { n++; return true; }
  } c;
  int next = 0, slices = 0;
  while (next >= 0) {
    next = a.scan(next, 16, c);
    slices++;
  }
  CHECK_EQ(c.n, 300);
  CHECK_EQ(slices, 8);                  // 128 buckets, 16 at a time
  CHECK_EQ(a.knownCount(), 300);

  struct StopAt : NodeVisitor {
    int n = 0;
    bool visit(const NodeRecord&) override { return ++n < 5; }
  } stop;
  CHECK_EQ(a.scan(0, NodeArchive::BUCKETS, stop), -1);
  CHECK_EQ(stop.n, 5);

  NodeRecord first = node(0, 0, "", 0, 0);
  first.pubKey[2] = 0;
  CHECK(a.remove(first.pubKey));
  CHECK(!a.remove(first.pubKey));
  CHECK_EQ(a.knownCount(), 299);
}

TEST(node_archive_survives_a_power_cut_during_an_update) {
  using namespace node_test;
  const NodeRecord before = node(0x50, 1, "Before", CONTACT_CHAT, T0);
  NodeRecord after = node(0x50, 1, "After", CONTACT_CHAT, T0 + 10);
  for (uint32_t step = 0; step < 5; step++) {
    FakeStorage disk;
    {
      NodeArchive a(disk);
      CHECK(a.upsert(before) == NodeArchive::Result::Added);
      CHECK(a.upsert(node(0x51, 2, "Neighbour", CONTACT_CHAT, T0)) == NodeArchive::Result::Added);
      disk.cutAt(step, 40);
      CHECK(a.upsert(after) == NodeArchive::Result::Failed);
    }
    disk.reboot();
    NodeArchive a(disk);
    NodeRecord r;
    CHECK(a.get(before.pubKey, r));
    CHECK(strcmp(r.name, "Before") == 0 || strcmp(r.name, "After") == 0);
    CHECK(a.get(node(0x51, 2, "", 0, 0).pubKey, r));   // the other node of the bucket is still there
    CHECK(a.upsert(node(0x50, 1, "Later", CONTACT_CHAT, T0 + 20)) == NodeArchive::Result::Updated);
    CHECK(a.get(before.pubKey, r));
    CHECK(strcmp(r.name, "Later") == 0);
  }
}

TEST(node_archive_reads_a_damaged_bucket_as_empty) {
  using namespace node_test;
  FakeStorage disk;
  {
    NodeArchive a(disk);
    CHECK(a.upsert(node(0x90, 1, "Lost", CONTACT_CHAT, T0)) == NodeArchive::Result::Added);
  }
  disk.files["/nb48"][40] ^= 0xFF;
  NodeArchive a(disk);
  NodeRecord r;
  g_coreLogCapture.clear();
  CHECK(!a.get(node(0x90, 1, "", 0, 0).pubKey, r));
  CHECK(logHas("/nb48: CRC"));
  CHECK(a.upsert(node(0x90, 2, "New", CONTACT_CHAT, T0)) == NodeArchive::Result::Added);
  CHECK(a.get(node(0x90, 2, "", 0, 0).pubKey, r));
}

// ============================================================ StoreBench (MESSAGING-12, step 3)

TEST(timing_stats_percentiles) {
  TimingStats t;
  for (uint32_t i = 1; i <= 100; i++) t.add(i * 1000);
  CHECK_EQ(t.count(), 100);
  CHECK_EQ(t.min(), 1000u);
  CHECK_EQ(t.max(), 100000u);
  CHECK_EQ(t.avg(), 50500u);
  CHECK_EQ(t.percentile(95), 95000u);
  CHECK_EQ(t.percentile(50), 50000u);
  char line[96];
  t.format(line, sizeof(line));
  CHECK(strstr(line, "p95 95.0") != nullptr);
}

TEST(store_bench_runs_every_phase_and_leaves_nothing) {
  struct Host : StoreBenchHost {
    FakeStorage* disk = nullptr;
    uint32_t t = 0;
    std::vector<std::string> lines;
    uint32_t micros() override { return t += 250; }
    int64_t fsUsedBytes() override {   // like LittleFS: a block per file
      int64_t n = 0;
      for (auto& f : disk->files) n += (int64_t)((f.second.size() + 4095) / 4096) * 4096;
      return n;
    }
    void log(bool, const char* line) override { lines.push_back(line); }
  } host;
  FakeStorage disk;
  host.disk = &disk;
  disk.files["/contacts.cp"] = std::vector<uint8_t>(100, 1);   // the user's files are not touched
  StoreBench::Options o;
  o.lookups = 40;
  std::unique_ptr<StoreBench> bench(new StoreBench(disk, host, o));
  int steps = 0;
  while (bench->step()) steps++;
  const StoreBench::Result& r = bench->result();
  CHECK(r.ok);
  CHECK(bench->phase() == StoreBench::Phase::Done);
  CHECK_EQ(r.smallSave.count(), 128);
  CHECK_EQ(r.fullSave.count(), 128);
  CHECK_EQ(r.read.count(), 128);
  CHECK_EQ(r.rewrite.count(), 128);
  CHECK_EQ(r.lookup.count(), 40);
  CHECK_EQ(r.remove.count(), 128);
  CHECK_EQ(steps + 1, 1 + 128 * 5 + 40);   // one storage operation per step (the last one returns false)
  CHECK_EQ(r.usedFull - r.usedBefore, 128 * 4096);
  CHECK_EQ(r.usedAfter, r.usedBefore);
  CHECK_EQ(disk.files.size(), 1u);
  CHECK(disk.exists("/contacts.cp"));
  bench->report();
  bool sawResult = false;
  for (auto& l : host.lines) sawResult |= l == "RESULT ok";
  CHECK(sawResult);
}

TEST(store_bench_reports_a_storage_failure) {
  struct Host : StoreBenchHost {
    uint32_t t = 0;
    std::string all;
    uint32_t micros() override { return t += 100; }
    int64_t fsUsedBytes() override { return -1; }
    void log(bool, const char* line) override { all += line; all += "\n"; }
  } host;
  FakeStorage disk;
  StoreBench bench(disk, host, StoreBench::Options());
  for (int i = 0; i < 20; i++) bench.step();
  disk.failWrites(true);
  while (bench.step()) {}
  CHECK(!bench.result().ok);
  CHECK(bench.phase() == StoreBench::Phase::Failed);
  bench.report();
  CHECK(host.all.find("RESULT FAILED at small buckets: save failed, bucket 19") != std::string::npos);
}

// ============================================================ NodeBook (MESSAGING-12, step 4)

TEST(node_book_waits_then_writes_a_bucket_at_a_time) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive archive(disk);
  NodeBook book(archive);
  uint32_t now = 1000;
  // A burst: 10 adverts of 4 nodes, two of them in the same bucket (0x20, 0x21)
  for (int round = 0; round < 10; round++) {
    CHECK(book.heard(node(0x20, 1, "A", CONTACT_CHAT, T0 + round), now));
    CHECK(book.heard(node(0x21, 1, "B", CONTACT_REPEATER, T0 + round), now));
    CHECK(book.heard(node(0x80, 1, "C", CONTACT_REPEATER, T0 + round), now));
    CHECK(book.heard(node(0xC0, 1, "D", CONTACT_ROOM, T0 + round), now));
    now += 500;
  }
  CHECK_EQ(disk.writes, 0u);            // nothing on flash yet
  CHECK_EQ(book.pending(), 4);
  CHECK_EQ(book.stats().merged, 36u);
  NodeRecord r;
  CHECK(book.get(node(0x20, 1, "", 0, 0).pubKey, r));   // but the lookups see them
  CHECK_EQ(r.lastHeard, T0 + 9);
  CHECK_EQ(r.firstHeard, T0);
  const uint8_t id[] = {0x80};
  CHECK_EQ(book.findByHash(id, 1, NodeArchive::ALL_TYPES, &r, 1), 1);
  CHECK(strcmp(r.name, "C") == 0);

  CHECK(!book.flushOne(now));           // under a minute old and few: nothing yet
  now = 1000 + NodeBook::Policy().maxAgeMs;
  CHECK(book.flushOne(now));            // the oldest bucket: A and B in one write
  CHECK_EQ(book.pending(), 2);
  CHECK_EQ(book.stats().writes, 1u);
  CHECK(!book.flushOne(now + 100));     // the gap between writes
  CHECK(book.flushOne(now + NodeBook::Policy().minGapMs));
  CHECK(book.flushOne(now + 2 * NodeBook::Policy().minGapMs));
  CHECK_EQ(book.pending(), 0);
  CHECK(!book.flushOne(now + 10 * NodeBook::Policy().minGapMs));
  CHECK_EQ(book.stats().writes, 3u);    // 40 adverts, 3 bucket writes

  NodeArchive after(disk);              // all on flash
  CHECK(after.get(node(0x21, 1, "", 0, 0).pubKey, r));
  CHECK(strcmp(r.name, "B") == 0);
  CHECK_EQ(r.lastHeard, T0 + 9);
  CHECK_EQ(r.firstHeard, T0);
}

TEST(node_book_full_queue_drops_and_writes_without_waiting) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive archive(disk);
  NodeBook book(archive);
  for (int i = 0; i < NodeBook::QUEUE; i++) CHECK(book.heard(node((uint8_t)(i * 10), 1, "n", CONTACT_CHAT, T0), 0));
  CHECK(!book.heard(node(0xFF, 1, "one more", CONTACT_CHAT, T0), 0));   // full: refused, counted, nothing written
  CHECK_EQ(book.stats().dropped, 1u);
  CHECK_EQ(disk.writes, 0u);
  CHECK(book.flushOne(1));              // crowded: no waiting for maxAge or the gap
  CHECK(book.flushOne(2));
  CHECK_EQ(book.stats().writes, 2u);
  CHECK_EQ(book.flushAll(), NodeBook::QUEUE - 2);
  CHECK_EQ(book.pending(), 0);
}

TEST(node_book_keeps_changes_the_flash_refused) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive archive(disk);
  NodeBook book(archive);
  book.heard(node(0x33, 1, "Kept", CONTACT_CHAT, T0), 0);
  disk.failWrites(true);
  CHECK_EQ(book.flushAll(), 1);
  CHECK_EQ(book.pending(), 1);          // still waiting, not lost
  CHECK_EQ(book.stats().failed, 1u);
  disk.failWrites(false);
  CHECK_EQ(book.flushAll(), 1);
  CHECK_EQ(book.pending(), 0);
  NodeRecord r;
  CHECK(archive.get(node(0x33, 1, "", 0, 0).pubKey, r));
}

TEST(node_book_flags_and_removals_wait_like_hearings) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive archive(disk);
  NodeBook book(archive);
  const NodeRecord a = node(0x44, 1, "Friend", CONTACT_CHAT, T0);
  book.heard(a, 0);
  book.markFlags(a.pubKey, NodeRecord::Keep | NodeRecord::InContacts, 0, 0);   // with the hearing: one write
  NodeRecord r;
  CHECK(book.get(a.pubKey, r));
  CHECK(r.has(NodeRecord::Keep));
  CHECK_EQ(disk.writes, 0u);            // RAM only
  CHECK_EQ(book.flushAll(), 1);
  CHECK(archive.get(a.pubKey, r));
  CHECK(r.has(NodeRecord::Keep) && r.has(NodeRecord::InContacts));

  book.markFlags(a.pubKey, 0, NodeRecord::InContacts, 0);   // a flag change alone
  CHECK(book.get(a.pubKey, r));
  CHECK(!r.has(NodeRecord::InContacts));
  CHECK_EQ(book.flushAll(), 1);
  CHECK(archive.get(a.pubKey, r));
  CHECK(!r.has(NodeRecord::InContacts) && r.has(NodeRecord::Keep));

  book.forget(a.pubKey, 0);
  CHECK(!book.get(a.pubKey, r));        // forgotten at once for the lookups
  const uint8_t id[] = {0x44};
  CHECK_EQ(book.findByHash(id, 1, NodeArchive::ALL_TYPES, nullptr, 0), 0);
  CHECK(archive.get(a.pubKey, r));      // still on flash until the write
  CHECK_EQ(book.flushAll(), 1);
  CHECK(!archive.get(a.pubKey, r));

  book.markFlags(node(0x45, 9, "", 0, 0).pubKey, NodeRecord::Favourite, 0, 0);   // flags for an unknown node
  CHECK_EQ(book.flushAll(), 1);
  CHECK_EQ(book.stats().refused, 1u);
  CHECK_EQ(book.pending(), 0);
}

TEST(node_book_lookups_count_waiting_and_stored_nodes_once) {
  using namespace node_test;
  FakeStorage disk;
  NodeArchive archive(disk);
  NodeBook book(archive);
  book.heard(node(0x46, 1, "Stored", CONTACT_REPEATER, T0), 0);
  book.flushAll();
  book.heard(node(0x46, 1, "Stored renamed", CONTACT_REPEATER, T0 + 5000), 0);
  book.heard(node(0x46, 2, "New", CONTACT_REPEATER, T0), 0);
  book.heard(node(0x47, 2, "Other ID", CONTACT_REPEATER, T0), 0);
  const uint8_t id[] = {0x46};
  NodeRecord out[4];
  CHECK_EQ(book.findByHash(id, 1, NodeArchive::ALL_TYPES, out, 4), 2);
  bool renamed = false, fresh = false;
  for (int i = 0; i < 2; i++) {
    renamed |= strcmp(out[i].name, "Stored renamed") == 0;
    fresh |= strcmp(out[i].name, "New") == 0;
  }
  CHECK(renamed && fresh);
  CHECK_EQ(book.findByHash(id, 1, NodeArchive::typeBit(CONTACT_CHAT), out, 4), 0);

  book.flushAll();                      // three stored with that ID; the middle one forgotten
  book.heard(node(0x46, 3, "Third", CONTACT_REPEATER, T0), 0);
  book.flushAll();
  book.forget(node(0x46, 2, "", 0, 0).pubKey, 0);
  CHECK_EQ(book.findByHash(id, 1, NodeArchive::ALL_TYPES, out, 4), 2);
  CHECK(strcmp(out[0].name, "New") != 0 && strcmp(out[1].name, "New") != 0);
  CHECK(strcmp(out[0].name, "Third") == 0 || strcmp(out[1].name, "Third") == 0);
}

TEST(node_book_a_change_during_a_write_is_not_lost) {
  // The storage task copies a batch, then writes it without the queue's lock; loop() may change the same node
  // meanwhile. Here the "meanwhile" is a FakeStorage write failure retried later, and a hearing merged between.
  using namespace node_test;
  FakeStorage disk;
  NodeArchive archive(disk);
  struct HearDuringWrite : Lock {   // the archive's lock: runs a hearing while the write is under way
    NodeBook* book = nullptr;
    bool armed = false;
    void lock() override {
      if (armed && book) {
        armed = false;
        book->heard(node_test::node(0x55, 1, "Renamed meanwhile", CONTACT_CHAT, node_test::T0 + 9000), 0);
      }
    }
    void unlock() override {}
  } archiveLock;
  NodeBook book(archive, nullptr, &archiveLock);
  archiveLock.book = &book;
  book.heard(node(0x55, 1, "First", CONTACT_CHAT, T0), 0);
  archiveLock.armed = true;             // the next write sees a hearing arrive
  CHECK_EQ(book.flushAll(), 2);         // the first write, then the change that came during it
  NodeRecord r;
  CHECK(archive.get(node(0x55, 1, "", 0, 0).pubKey, r));
  CHECK(strcmp(r.name, "Renamed meanwhile") == 0);
  CHECK_EQ(book.pending(), 0);
}

TEST(node_match_cache_answers_until_it_is_stale) {
  NodeMatchCache c;
  NodeMatch m[6];
  for (int i = 0; i < 6; i++) snprintf(m[i].name, sizeof(m[i].name), "R%d", i);
  const uint8_t a[] = {0xA1}, b[] = {0xA1, 0x02};
  NodeMatch out[6];
  int count = -1;
  CHECK(!c.get(a, 1, true, 0, out, 4, count));
  c.put(a, 1, true, 1000, m, 2);
  CHECK(c.get(a, 1, true, 2000, out, 4, count));
  CHECK_EQ(count, 2);
  CHECK(strcmp(out[1].name, "R1") == 0);
  CHECK(!c.get(a, 1, false, 2000, out, 4, count));   // relays only and every node: two questions
  CHECK(!c.get(b, 2, true, 2000, out, 4, count));    // a longer ID: another question
  CHECK(!c.get(a, 1, true, 1000 + NodeMatchCache::TTL_MS, out, 4, count));   // stale
  c.put(a, 1, true, 5000, m, 6);                     // more than it keeps
  CHECK(c.get(a, 1, true, 5001, out, 1, count));     // enough for a caller that wants one
  CHECK_EQ(count, 6);
  CHECK(!c.get(a, 1, true, 5001, out, 6, count));    // not for one that wants them all
  for (int i = 0; i < NodeMatchCache::SIZE + 3; i++) {   // the oldest answers make room
    const uint8_t h[] = {(uint8_t)i, 7};
    c.put(h, 2, false, 6000 + (uint32_t)i, m, 1);
  }
  const uint8_t newest[] = {(uint8_t)(NodeMatchCache::SIZE + 2), 7};
  CHECK(c.get(newest, 2, false, 7000, out, 1, count));
  c.clear();
  CHECK(!c.get(newest, 2, false, 7000, out, 1, count));
}

// ============================================================ Mentions (MESSAGING-13)

TEST(mentions_are_found_with_spaces_and_accents) {
  const char* t = "hi @[Élodie B] and @[Bob], not @Bob nor @[open or @[] or a@[x]";
  MentionSpan s[8];
  const int n = MentionText::find(t, s, 8);
  CHECK_EQ(n, 3);                       // "a@[x]" counts too: the format, not the context, makes a mention
  CHECK_EQ(s[0].start, 3);
  CHECK(strncmp(t + s[0].nameStart, "Élodie B", s[0].nameLen) == 0);
  CHECK(strncmp(t + s[1].nameStart, "Bob", s[1].nameLen) == 0);
  CHECK(MentionText::mentions(t, "elodie b"));   // case and accents ignored
  CHECK(MentionText::mentions(t, "BOB"));
  CHECK(!MentionText::mentions(t, "Bo"));
  CHECK(!MentionText::mentions(t, ""));
  CHECK(!MentionText::mentions("@[" "abcdefghijabcdefghijabcdefghijabc]", "abcdefghijabcdefghijabcdefghijabc"));   // too long
}

TEST(mentions_display_without_brackets) {
  char out[64];
  MentionSpan s[4];
  const int n = MentionText::toDisplay("@[Ann Lee]: ok @[Bob]!", out, sizeof(out), s, 4);
  CHECK(strcmp(out, "@Ann Lee: ok @Bob!") == 0);
  CHECK_EQ(n, 2);
  CHECK_EQ(s[0].start, 0);
  CHECK_EQ(s[0].len, 8);
  CHECK_EQ(s[1].start, 13);
  CHECK(strncmp(out + s[1].nameStart, "Bob", s[1].nameLen) == 0);
  CHECK_EQ(MentionText::toDisplay("plain text", out, sizeof(out), s, 4), 0);
  CHECK(strcmp(out, "plain text") == 0);
  char tiny[6];
  MentionText::toDisplay("@[Alexandra]", tiny, sizeof(tiny), s, 4);   // cut, still terminated
  CHECK_EQ(strlen(tiny), 5u);
}

TEST(mentions_fold_french_letters) {
  char f[32];
  MentionText::fold("ÉLÈVE Çà Ôù", f, sizeof(f));
  CHECK(strcmp(f, "eleve ca ou") == 0);
  CHECK(MentionText::sameName("Zoé", "ZOE"));
  CHECK(!MentionText::sameName("Zoé", "Zo"));
}

namespace mention_test {
struct Names : MentionSource {
  std::vector<std::string> v;
  int count() override { return (int)v.size(); }
  bool name(int i, char* out, size_t n) override { snprintf(out, n, "%s", v[i].c_str()); return true; }
};
}

TEST(mention_picker_follows_the_typing) {
  mention_test::Names src;
  src.v = {"Élodie", "Bob", "Uncle Bob", "bobby", "Alice", "Bob"};   // favourites first; "Bob" twice (contact + sender)
  MentionPicker p;
  char draft[64] = "hello";
  p.update(draft, src);
  CHECK(!p.open());
  strcat(draft, " @");
  p.update(draft, src);
  CHECK(p.open());
  CHECK_EQ(p.count(), MentionPicker::MAX);   // every name, in order
  CHECK(strcmp(p.suggestion(0), "Élodie") == 0);
  strcat(draft, "bo");
  p.update(draft, src);
  CHECK_EQ(p.count(), 3);                    // Bob, bobby first (start), then Uncle Bob (a word); Bob once
  CHECK(strcmp(p.suggestion(0), "Bob") == 0);
  CHECK(strcmp(p.suggestion(1), "bobby") == 0);
  CHECK(strcmp(p.suggestion(2), "Uncle Bob") == 0);
  p.move(1);
  p.move(1);
  CHECK(p.accept(draft, sizeof(draft)));
  CHECK(strcmp(draft, "hello @[Uncle Bob] ") == 0);
  CHECK(!p.open());
  p.update(draft, src);                      // typing goes on: no reopening
  CHECK(!p.open());

  char d2[64] = "@el";                       // at the start of the text
  p.update("@", src);
  p.update(d2, src);
  CHECK(p.open() && p.count() == 1);
  d2[0] = 0;                                 // the "@" deleted
  p.update(d2, src);
  CHECK(!p.open());

  char mail[64] = "me@";                     // inside a word: an address, not a mention
  p.update(mail, src);
  CHECK(!p.open());

  char full[24] = "x @";
  p.update(full, src);
  strcat(full, "unc");
  p.update(full, src);
  CHECK(!p.accept(full, 12));                // "@[Uncle Bob] " does not fit: refused, draft unchanged
  CHECK(strcmp(full, "x @unc") == 0);
}


TEST(notify_policy_follows_the_silent_mode) {
  MeshEvent dm, ch, me, fail, fresh;
  dm.type = MeshEvent::MESSAGE;
  dm.key.kind = ConvKey::DIRECT;
  ch.type = MeshEvent::MESSAGE;
  ch.key.kind = ConvKey::CHANNEL;
  me.type = MeshEvent::MENTION;
  me.key.kind = ConvKey::CHANNEL;
  fail.type = MeshEvent::FAILED;
  fresh.type = MeshEvent::CONTACT_NEW;
  // Everything (the default): as before the silent mode
  CHECK(NotifyPolicy::decide(ch, WAKE_ALL).wake && NotifyPolicy::decide(ch, WAKE_ALL).sound);
  CHECK(NotifyPolicy::decide(ch, WAKE_ALL).soundKind == SOUND_CHANNEL_MSG);
  CHECK(NotifyPolicy::decide(dm, WAKE_ALL).soundKind == SOUND_DIRECT_MSG);
  CHECK(NotifyPolicy::decide(fresh, WAKE_ALL).wake && !NotifyPolicy::decide(fresh, WAKE_ALL).sound);
  CHECK(NotifyPolicy::decide(me, WAKE_ALL).soundKind == SOUND_DIRECT_MSG);   // a mention sounds like a DM
  // For me only: a busy channel stays dark and quiet, a DM or a mention does not
  CHECK(!NotifyPolicy::decide(ch, WAKE_TO_ME).wake && !NotifyPolicy::decide(ch, WAKE_TO_ME).sound);
  CHECK(NotifyPolicy::decide(ch, WAKE_TO_ME).banner);                        // shown if the screen is on
  CHECK(NotifyPolicy::decide(dm, WAKE_TO_ME).wake && NotifyPolicy::decide(dm, WAKE_TO_ME).sound);
  CHECK(NotifyPolicy::decide(me, WAKE_TO_ME).wake && NotifyPolicy::decide(me, WAKE_TO_ME).sound);
  CHECK(!NotifyPolicy::decide(fail, WAKE_TO_ME).wake);
  // Silent: nothing wakes or sounds
  CHECK(!NotifyPolicy::decide(dm, WAKE_NEVER).wake && !NotifyPolicy::decide(dm, WAKE_NEVER).sound);
  CHECK(!NotifyPolicy::decide(me, WAKE_NEVER).wake);
  CHECK(NotifyPolicy::decide(dm, WAKE_NEVER).banner);
  CHECK(!NotifyPolicy::soundOnScreen(ch, WAKE_NEVER));
  CHECK(!NotifyPolicy::soundOnScreen(ch, WAKE_TO_ME));
  CHECK(NotifyPolicy::soundOnScreen(me, WAKE_TO_ME));
  CHECK(NotifyPolicy::soundOnScreen(ch, WAKE_ALL));
  MeshEvent ack;
  ack.type = MeshEvent::DELIVERED;
  CHECK(!NotifyPolicy::decide(ack, WAKE_ALL).banner && !NotifyPolicy::decide(ack, WAKE_ALL).wake);
}

// ============================================================ Radio presets outside the EU

TEST(radio_presets_keep_saved_indexes_and_follow_the_app) {
  // The first three are what earlier firmwares saved: they must keep their values
  CHECK(strcmp(RADIO_PRESETS[0].name, "EU/UK Narrow") == 0);
  CHECK(fabsf(RADIO_PRESETS[0].freqMHz - 869.618f) < 0.0005f && RADIO_PRESETS[0].sf == 8 && RADIO_PRESETS[0].cr == 5);
  CHECK(RADIO_PRESETS[1].sf == 11 && RADIO_PRESETS[2].sf == 10);
  // Every preset is a frequency the Cap is built for, with valid LoRa values
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
    const RadioPreset& p = RADIO_PRESETS[i];
    CHECK(RadioConfig::inCapBand(p.freqMHz));
    CHECK(fabsf(RadioConfig::BANDWIDTHS[RadioConfig::bandwidthIndex(p.bwKHz)] - p.bwKHz) < 0.01f);
    CHECK(p.sf >= 5 && p.sf <= 12 && p.cr >= 5 && p.cr <= 8);
    CHECK(strlen(p.name) <= 18);
  }
  // The display order lists each once
  int seen[RADIO_PRESET_COUNT] = {};
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) seen[RADIO_PRESET_ORDER[i]]++;
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) CHECK_EQ(seen[i], 1);
}

TEST(radio_config_presets_and_custom_values) {
  UiSettings s;
  RadioParams p = RadioConfig::of(s);
  CHECK(fabsf(p.freqMHz - 869.618f) < 0.0005f && p.sf == 8);
  // Choosing a preset whose network uses 3-byte path IDs (Canada) sets them too
  s.pathHashBytes = 1;
  CHECK(RadioConfig::choose(s, 4));
  CHECK_EQ(s.pathHashBytes, 3);
  CHECK(!RadioConfig::choose(s, 3));                  // USA: no path size in the preset, the setting stays
  CHECK_EQ(s.pathHashBytes, 3);
  CHECK(fabsf(RadioConfig::of(s).freqMHz - 910.525f) < 0.0005f && RadioConfig::of(s).sf == 7);
  // Custom starts from the values in use, then keeps its own
  RadioConfig::choose(s, RADIO_PRESET_CUSTOM);
  CHECK(RadioConfig::isCustom(s));
  CHECK(RadioConfig::of(s) == RadioConfig::of([] { UiSettings u; u.radioPreset = 3; return u; }()));
  RadioParams c;
  c.freqMHz = 433.375f;
  c.bwKHz = 60.0f;                                     // snapped to 62.5
  c.sf = 14;                                           // clamped to 12
  c.cr = 6;
  RadioConfig::setCustom(s, c);
  p = RadioConfig::of(s);
  CHECK(fabsf(p.freqMHz - 433.375f) < 0.0005f && fabsf(p.bwKHz - 62.5f) < 0.01f && p.sf == 12 && p.cr == 6);
  CHECK(!RadioConfig::inCapBand(p.freqMHz));
  s.customFreqKHz = 5;                                 // a damaged file: the default frequency, never a wrong one
  CHECK(fabsf(RadioConfig::of(s).freqMHz - 869.618f) < 0.0005f);
  char d[40];
  RadioConfig::describe(RadioConfig::of(s), d, sizeof(d));
  CHECK(strcmp(d, "869.618 BW62.5 SF12 CR6") == 0);
}

TEST(radio_config_steps_through_the_display_order) {
  UiSettings s;
  RadioConfig::step(s, -1);                            // before the first: Custom
  CHECK(RadioConfig::isCustom(s));
  RadioConfig::step(s, +1);                            // after Custom: the first again
  CHECK_EQ(s.radioPreset, RADIO_PRESET_ORDER[0]);
  RadioConfig::step(s, +1);
  CHECK_EQ(s.radioPreset, RADIO_PRESET_ORDER[1]);
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) RadioConfig::step(s, +1);   // a full turn plus Custom
  CHECK_EQ(s.radioPreset, RADIO_PRESET_ORDER[0]);
}

TEST(radio_config_reads_frequencies_as_people_type_them) {
  float f = 0;
  CHECK(RadioConfig::parseFrequency("910.525", f) && fabsf(f - 910.525f) < 0.0005f);
  CHECK(RadioConfig::parseFrequency("869,618", f) && fabsf(f - 869.618f) < 0.0005f);   // French decimal comma
  CHECK(RadioConfig::parseFrequency(" 923 ", f) && fabsf(f - 923.0f) < 0.0005f);
  CHECK(!RadioConfig::parseFrequency("2400", f));     // not an SX1262 frequency
  CHECK(!RadioConfig::parseFrequency("91O.5", f));    // a letter O
  CHECK(!RadioConfig::parseFrequency("", f));
}

// ============================================================ Themes

TEST(built_in_themes_are_readable) {
  ThemeCatalog c;
  CHECK_EQ(c.count(), ThemeCatalog::BUILTIN);
  for (int i = 0; i < c.count(); i++) {
    const char* weak[16];
    const int n = c.at(i).weakPairs(weak, 16);
    for (int k = 0; k < n && k < 16; k++) printf("    theme %s: %s too close\n", c.at(i).name, weak[k]);
    CHECK_EQ(n, 0);
  }
  // Dark is the original palette, unchanged
  CHECK_EQ(c.find("dark")[Palette::ACCENT], 0x05FF);
  CHECK_EQ(c.find("Dark")[Palette::BUBBLE_OUT], 0x122D);
  CHECK(Palette::contrast(0x0000, 0xFFFF) > 20.9f);
  CHECK(Palette::contrast(0x8410, 0x8410) < 1.01f);
}

TEST(theme_files_override_a_base) {
  ThemeCatalog c;
  char err[80];
  int warn = 0;
  const char* ocean =
      "# a theme for the sea\n"
      "background = #001830\n"
      "base = Light\n"                     // after a colour: the order does not matter
      "name = Ocean\n"
      "text=#E8F4FF\n"
      "accent = 00C0FF\n";
  CHECK(c.add(ocean, "ocean", err, sizeof(err), warn));
  CHECK_EQ(warn, 0);
  const Palette& o = c.find("ocean");
  CHECK(strcmp(o.name, "Ocean") == 0);
  CHECK_EQ(o[Palette::BG], Palette::rgb565(0x00, 0x18, 0x30));
  CHECK_EQ(o[Palette::FG], Palette::rgb565(0xE8, 0xF4, 0xFF));
  CHECK_EQ(o[Palette::OK], c.find("Light")[Palette::OK]);    // from the base
  CHECK_EQ(c.count(), ThemeCatalog::BUILTIN + 1);
  CHECK(strcmp(c.step("Ocean", 1), "Dark") == 0);              // the list wraps around
  CHECK(strcmp(c.step("Dark", -1), "Ocean") == 0);

  CHECK(c.add("name = Mixed\ntext = #FFFFFF\ncolour = #123456\naccent = blue\nno equals sign\n", "mixed", err, sizeof(err), warn));
  CHECK_EQ(warn, 3);                       // kept, with its problems counted
  CHECK(strstr(err, "line 3: unknown key 'colour'") != nullptr);
  CHECK(!c.add("# nothing\n\n", "empty", err, sizeof(err), warn));
  CHECK(strstr(err, "no colour") != nullptr);
  CHECK(!c.add("text = #FFFFFF\n", "Dark", err, sizeof(err), warn));   // a built-in name
  CHECK(strstr(err, "already exists") != nullptr);
  CHECK(strcmp(c.find("removed from the card").name, "Dark") == 0);   // a missing theme: Dark
  uint16_t v;
  CHECK(ThemeCatalog::parseColor("#FF0000", v) && v == 0xF800);
  CHECK(!ThemeCatalog::parseColor("#FF00", v));
  CHECK(!ThemeCatalog::parseColor("#GG0000", v));
}

// ============================================================ Navigation menu

TEST(carousel_slides_and_wraps) {
  Carousel c;
  c.setCount(4);
  CHECK_EQ(c.page(), 0);
  CHECK(!c.sliding(0));
  c.slide(-1, 1000);                  // left of the first: the last
  CHECK_EQ(c.page(), 3);
  CHECK_EQ(c.leaving(1000), 0);
  CHECK(c.sliding(1000));
  CHECK_EQ(c.shift(1000, 240), -240); // arrives from the left
  CHECK(c.shift(1080, 240) > -240 && c.shift(1080, 240) < 0);
  CHECK_EQ(c.shift(1000 + Carousel::SLIDE_MS, 240), 0);
  CHECK_EQ(c.leaving(1000 + Carousel::SLIDE_MS), -1);
  c.slide(+1, 2000);
  CHECK_EQ(c.page(), 0);              // right of the last: the first
  CHECK_EQ(c.shift(2000, 240), 240);
  c.go(2, 3000);
  CHECK_EQ(c.page(), 2);
  CHECK(c.shift(3000, 100) == 100);   // forward: from the right
  c.go(9, 4000);                      // not a page: nothing happens
  CHECK_EQ(c.page(), 2);
  c.setCount(1);                      // one page: no slide
  c.slide(1, 5000);
  CHECK_EQ(c.page(), 0);
  CHECK(!c.sliding(5000));
}

// ============================================================ Live packet tracer

namespace tracer_test {
// A flood heard: an advert from `sender` (2 bytes of key) through `path` (1-byte IDs)
void flood(TrafficTracer& t, uint32_t ms, uint8_t s0, uint8_t s1, std::vector<uint8_t> path, bool tx = false) {
  uint8_t body[40] = {};
  body[0] = s0;
  body[1] = s1;
  PacketLogFrame f;
  f.header = wire::header(wire::ROUTE_FLOOD, wire::PL_ADVERT);
  f.pathLen = (uint8_t)path.size();
  f.path = path.data();
  f.payload = body;
  f.payloadLen = sizeof(body);
  f.ms = ms;
  f.tx = tx;
  PacketLog log;                   // the real decoding of the log, as on the device
  log.setObserver(&t);
  log.add(f);
}
int find(const TrafficTracer& t, uint8_t id0) {
  for (int i = 0; i < TrafficTracer::MAX_NODES; i++) {
    if (t.node(i).used && t.node(i).kind != TrafficTracer::Kind::Me && t.node(i).id[0] == id0) return i;
  }
  return -1;
}
}  // namespace tracer_test

TEST(tracer_starts_blank_and_traces_a_route) {
  using namespace tracer_test;
  TrafficTracer t;
  CHECK_EQ(t.nodes(), 0);           // T0: nothing shown
  CHECK_EQ(t.me(), -1);
  flood(t, 1000, 0x5A, 0x01, {0xA1, 0xB2});
  CHECK_EQ(t.nodes(), 4);           // the sender, two repeaters, me
  CHECK_EQ(t.links(), 3);
  const int sender = find(t, 0x5A), a = find(t, 0xA1), b = find(t, 0xB2);
  CHECK(t.node(sender).kind == TrafficTracer::Kind::Client);
  CHECK(t.node(a).kind == TrafficTracer::Kind::Router);
  CHECK_EQ(t.node(b).hopsFromMe, 1);  // the last repeater is next to me
  CHECK_EQ(t.node(a).hopsFromMe, 2);
  CHECK_EQ(t.node(sender).hopsFromMe, 3);
  CHECK_EQ(t.usage(a), 1);
  // T1: a first message, its nodes green
  CHECK(TrafficTracer::heatColor(t.usage(a)) >> 11 <= 2);    // almost no red
  CHECK_EQ((TrafficTracer::heatColor(t.usage(a)) >> 5) & 0x3F, 63);
  flood(t, 2000, 0x5A, 0x01, {0xA1, 0xB2}, true);           // our own packets are not traced
  CHECK_EQ(t.usage(a), 1);
}

TEST(tracer_heats_busy_repeaters_and_forgets_after_20_minutes) {
  using namespace tracer_test;
  TrafficTracer t;
  uint32_t ms = 0;
  for (int i = 0; i < TrafficTracer::BUSY; i++) {
    flood(t, ms, (uint8_t)(0x10 + i % 5), 0x00, {0xA1});      // A1 relays everything
    ms += 20000;                                               // 3 per minute
  }
  const int a = find(t, 0xA1);
  CHECK_EQ(t.usage(a), TrafficTracer::BUSY);
  CHECK_EQ(TrafficTracer::heatColor(t.usage(a)), 0xF800);    // Tn: full red
  CHECK_EQ(TrafficTracer::heatColor(0) , 0x07E0);            // pure green at the start of the scale
  const uint16_t half = TrafficTracer::heatColor(TrafficTracer::BUSY / 2);
  CHECK((half >> 11) > 25 && ((half >> 5) & 0x3F) > 55);      // yellow in the middle, not a dull olive
  CHECK_EQ(t.nodes(), 1 + 5 + 1);
  // Quiet: after 20 minutes without packets, everything leaves
  t.advance(ms + 10 * 60000);
  CHECK(t.nodes() > 0);
  CHECK(t.usage(a) < TrafficTracer::BUSY);                   // the oldest minutes left the window
  t.advance(ms + 21 * 60000);
  CHECK_EQ(t.nodes(), 0);
  CHECK_EQ(t.links(), 0);
  CHECK_EQ(t.me(), -1);
}

TEST(tracer_merges_short_and_long_ids_and_keeps_its_budget) {
  using namespace tracer_test;
  TrafficTracer t;
  flood(t, 0, 0x01, 0x00, {0xA1});
  // The same repeater in a packet with 2-byte IDs
  uint8_t path2[2] = {0xA1, 0x77};
  uint8_t body[40] = {0x02, 0x00};
  PacketLogFrame f;
  f.header = wire::header(wire::ROUTE_FLOOD, wire::PL_ADVERT);
  f.pathLen = (1 << 6) | 1;
  f.path = path2;
  f.payload = body;
  f.payloadLen = sizeof(body);
  PacketLog log;
  log.setObserver(&t);
  log.add(f);
  const int a = find(t, 0xA1);
  CHECK_EQ(t.node(a).idBytes, 2);   // learnt a second byte
  CHECK_EQ(t.usage(a), 2);
  CHECK_EQ(t.nodes(), 4);           // two senders, A1, me
  // Many senders: the table stays within its size, the least used leave
  for (int i = 0; i < 100; i++) flood(t, 1000 + i, (uint8_t)i, 0x55, {0xA1});
  CHECK(t.nodes() <= TrafficTracer::MAX_NODES);
  CHECK(t.node(find(t, 0xA1)).used);                         // the busy repeater stays
  NodeMatch m[2];
  snprintf(m[0].name, sizeof(m[0].name), "Colomiers");
  m[0].hasPos = true;
  m[0].latE6 = 43611600;
  t.resolve(find(t, 0xA1), m, 1);
  CHECK(t.node(find(t, 0xA1)).lookup == TrafficTracer::Lookup::Known && t.node(find(t, 0xA1)).hasPos);
  t.resolve(find(t, 0xA1), m, 2);                            // two nodes share the ID: not placed
  CHECK(t.node(find(t, 0xA1)).lookup == TrafficTracer::Lookup::Ambiguous && !t.node(find(t, 0xA1)).hasPos);
  t.clear();
  CHECK_EQ(t.nodes(), 0);
}

int main(int argc, char** argv) {
  const char* only = argc > 1 ? argv[1] : nullptr;
  g_coreLogQuiet = getenv("CORE_LOG") == nullptr;
  int ran = 0;
  for (auto& t : tests()) {
    if (only && !strstr(t.name, only)) continue;
    g_test = t.name;
    int before = g_failed;
    t.fn();
    ran++;
    printf("%s %s\n", g_failed == before ? "ok  " : "FAIL", t.name);
  }
  printf("== unit tests: %d tests, %d checks, %d failure(s)\n", ran, g_checks, g_failed);
  return g_failed ? 1 : 0;
}
