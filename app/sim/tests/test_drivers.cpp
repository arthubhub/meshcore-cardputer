// Unit tests of the portable drivers (core/drivers) against fake chips (sim/fakes).
// Layer 1 of the test pyramid: each hardware block alone, no UI.
//   make -C app/sim test        (CORE_LOG=1 to see the driver logs)
#include "../../core/KeyMatrix.h"
#include "../../core/drivers/GpsLink.h"
#include "../../core/drivers/Pi4ioe.h"
#include "../../core/drivers/Tca8418.h"
#include "../fakes/FakeGpsUart.h"
#include "../fakes/FakeI2c.h"

#include <functional>
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

// ============================================================ main

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
