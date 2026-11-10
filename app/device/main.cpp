// Standalone Cardputer ADV firmware: keyboard messaging UI on top of MeshCore.
//
// Build variants (platformio.ini at the project root):
//   cardputer_adv_standalone       messaging, contacts, channels, GPS, compass, backup
//   cardputer_adv_standalone_map   + offline map from the SD card            (CPADV_MAP=1)
//   cardputer_adv_demo             no LoRa Cap needed: simulated network and GPS (CPADV_DEMO=1)
#include <Arduino.h>
#include <CpadvLog.h>
#include <CpadvDataFs.h>   // the data partition (shared with phone mode)
#include <M5Unified.h>
#include <helpers/ArduinoHelpers.h>
#include <target.h>

#include "../core/App.h"
#include "../core/screens/GpsScreens.h"
#include "../core/screens/HomeScreen.h"
#include "../core/screens/MessagingScreens.h"
#include "../core/screens/SettingsScreens.h"
#include "../core/screens/NearbyScreen.h"
#include "../core/screens/PacketsScreen.h"
#include "../core/screens/RouteScreen.h"
#include "DebugConsole.h"
#include <CpadvBoot.h>
#include "DeviceBackup.h"
#include "DeviceServices.h"
#include "LittleFsStorage.h"
#include "SdCard.h"
#include <CpadvRadioMonitor.h>
#include "SdTiles.h"
#include "Tca8418Keyboard.h"
#if CPADV_MAP
  #include "../core/screens/MapScreen.h"
#endif
#if CPADV_DEMO
  #include "../core/DemoGps.h"
  #include "../core/DemoMesh.h"
#else
  #include "CardputerMesh.h"
  #include "DeviceGps.h"
#endif

static M5Canvas canvas(&M5.Display);   // the single 240x135 RGB565 framebuffer (64.8 KB)
static Tca8418Keyboard keyboard;
static bool radioReady = false;
static DeviceSettings settings;
static DeviceBackup backup;

// ---------------------------------------------------------------- services
#if CPADV_DEMO
// The demo GPS needs the system clock and the system needs the GPS: a proxy breaks the cycle.
class GpsProxy : public GpsService {
public:
  GpsService* target = nullptr;
  const GpsState& state() override { static GpsState none; return target ? target->state() : none; }
  bool present() override { return target && target->present(); }
  uint32_t baud() override { return target ? target->baud() : 0; }
  void coldRestart() override { if (target) target->coldRestart(); }
};
static GpsProxy gpsProxy;
static LittleFsStorage demoStorage;
static DeviceSystem sys(settings, gpsProxy);
static DemoGps gpsDev(sys);
static DemoMesh meshNode(sys, settings, demoStorage);
#else
static DeviceGps gpsDev;
static DeviceSystem sys(settings, gpsDev);
static StdRNG fastRng;
static SimpleMeshTables tables;
static CardputerMesh meshNode(radio_driver, fastRng, rtc_clock, tables, settings);
#endif

#if CPADV_MAP
static SdTiles sdTiles;
static MapScreen mapScreen;
static TileSource* const tileSource = &sdTiles;
#else
static TileSource* const tileSource = nullptr;
#endif

static App app(Services{&meshNode, &sys, &settings, &gpsDev, tileSource, &backup}, canvas);

// ---------------------------------------------------------------- screens
static TextInputScreen textInput;
static ChatScreen chatScreen;
static ContactDetailScreen contactDetail(chatScreen);
static MessageDetailScreen messageDetail(chatScreen, contactDetail);
static RouteScreen routeScreen(contactDetail);
static ContactsScreen contactsScreen(chatScreen, contactDetail);
static ConversationsScreen conversations(chatScreen, contactsScreen, textInput);
static CompassScreen compassScreen;
static GpsScreen gpsScreen(compassScreen);
static SystemScreen systemScreen;
static PacketDetailScreen packetDetail;
static PacketsScreen packetsScreen(packetDetail);
static SettingsScreen settingsScreen(textInput, systemScreen, packetsScreen);
static NearbyScreen nearbyScreen(contactDetail);
#if CPADV_MAP
static HomeScreen home(&conversations, &contactsScreen, &gpsScreen, &mapScreen, &nearbyScreen, &settingsScreen);
#else
static HomeScreen home(&conversations, &contactsScreen, &gpsScreen, nullptr, &nearbyScreen, &settingsScreen);   // no map built in
#endif

// ---------------------------------------------------------------- power / screen
static uint32_t lastActivity = 0;
static bool screenAsleep = false;
static uint32_t lastHeapLog = 0;
static uint32_t lastBatteryCheck = 0;
static bool lowBatteryWarned = false;
static uint32_t bootAdvertAt = 0;
static uint8_t appliedBrightness = 0;

static uint8_t brightnessLevel() {
  static const uint8_t LEVELS[] = {40, 110, 200};
  uint8_t b = settings.get().brightness;
  return LEVELS[b < 3 ? b : 1];
}

static void meshLoop() {
#if CPADV_DEMO
  gpsDev.tick();
  meshNode.tick();
#else
  meshNode.loop();
#endif
}

static void wakeScreen() {
  if (!screenAsleep) return;
  setCpuFrequencyMhz(240);   // full speed for drawing
  M5.Display.wakeup();
  M5.Display.setBrightness(brightnessLevel());
  screenAsleep = false;
  app.invalidate();
  CPLOG_I("MAIN", "screen on");
}

static void sleepScreen() {
  if (screenAsleep) return;
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  screenAsleep = true;
  setCpuFrequencyMhz(80);    // radio, GPS and keyboard only: 80 MHz is plenty and saves battery
  CPLOG_I("MAIN", "screen off, CPU 80 MHz (radio and GPS keep running)");
}

#if CPADV_DEMO
// Build date as the demo clock (no GPS / radio to set it)
static uint32_t buildUnix() {
  static const char* const M = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
  int mo = (int)(strstr(M, mon) - M) / 3 + 1, d = atoi(__DATE__ + 4), y = atoi(__DATE__ + 7);
  int hh = atoi(__TIME__), mi = atoi(__TIME__ + 3), ss = atoi(__TIME__ + 6);
  y -= mo <= 2;
  int era = y / 400, yoe = y - era * 400;
  int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  uint32_t days = era * 146097 + doe - 719468;
  return days * 86400u + hh * 3600 + mi * 60 + ss;   // build machine time (CI: UTC)
}
#endif

// ---------------------------------------------------------------- serial diagnostics
static DebugConsole console;

// One status line every 10 s (called by board.loop()).
static void heartbeat() {
#if CPADV_DEMO
  CPLOG_I("BEAT", "up %lus heap %u min %u | bat %d%% | screen %s | keys %s | DEMO", millis() / 1000UL,
          ESP.getFreeHeap(), ESP.getMinFreeHeap(), M5.Power.getBatteryLevel(), screenAsleep ? "off" : "on",
          keyboard.ok() ? "ok" : "NO");
#else
  const GpsState& g = gpsDev.state();
  // Radio part: same summary as phone mode (variant/CpadvRadioMonitor), so both modes compare directly
  char radio[120] = "radio NOT READY";
  if (cpadv::RadioMonitor* m = board.radioMonitor()) m->summary(radio, sizeof(radio), millis());
  RadioStats rs = meshNode.radioStats();
  CPLOG_I("BEAT", "up %lus heap %u min %u | bat %d%% %umV | screen %s | keys %s %lu ev %lu err INT %s | %s | "
          "air %.2f%%/%g%% pkts %lu | gps %s %lu B %lu sent %s %u sats",
          millis() / 1000UL, ESP.getFreeHeap(), ESP.getMinFreeHeap(), M5.Power.getBatteryLevel(),
          board.getBattMilliVolts(), screenAsleep ? "off" : "on", keyboard.ok() ? "ok" : "NO",
          (unsigned long)keyboard.eventsRead(), (unsigned long)keyboard.i2cErrors(), keyboard.intLevel() ? "hi" : "LOW",
          radio, rs.txAirPercent, rs.txAirLimitPercent, (unsigned long)meshNode.packetLog().total(),
          gpsDev.searching() ? "searching" : "locked", (unsigned long)gpsDev.bytesReceived(),
          (unsigned long)g.sentences, g.valid ? "FIX" : "nofix", g.sats);
#endif
}

static bool argOn(const char* a) { return !strcmp(a, "on") || !strcmp(a, "1"); }

#if CPADV_DB_TEST
// ---------------------------------------------------------------- SQLite trial (docs/storage), env cardputer_adv_dual_dbtest
#include "DbProbe.h"
#include "SdCard.h"
#include <DbSchema.inc>   // docs/storage/schema.sql, embedded by tools/pio_dbschema.py
#include <SD.h>
#include <esp_heap_caps.h>
#include <memory>

// The platform side of the trial. Two ways to run it:
//  - in its own task (default): loop() keeps serving the radio, the screen and the GPS meanwhile, and the radio
//    counters before and after show the real impact;
//  - in loop() (task=0): the radio is served between transactions only, as in the first trial. Required for a
//    database on the SD card: the radio does not take the card's bus lock, it relies on running in the same task.
class DeviceDb : public DbProbeHost {
public:
  DeviceDb(bool inTask, bool onSd) : _inTask(inTask), _onSd(onSd) {}
  uint32_t ms() override { return ::millis(); }
  uint32_t micros() override { return ::micros(); }
  uint32_t freeHeap() override { return ESP.getFreeHeap(); }
  uint32_t largestBlock() override { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }
  uint32_t systemMinHeap() override { return ESP.getMinFreeHeap(); }
  uint32_t fsUsedKB() override {
    if (_onSd) { sdcard::Lock lock; return (uint32_t)(SD.usedBytes() / 1024); }
    return (uint32_t)(cpadv::dataFs().usedBytes() / 1024);
  }
  uint32_t fsFreeKB() override {
    if (_onSd) { sdcard::Lock lock; return (uint32_t)((SD.totalBytes() - SD.usedBytes()) / 1024); }
    return (uint32_t)((cpadv::dataFs().totalBytes() - cpadv::dataFs().usedBytes()) / 1024);
  }
  void service() override {
    if (_inTask) { vTaskDelay(1); return; }   // loop() serves the radio
    meshNode.loop();
    delay(1);
  }
  void log(bool error, const char* line) override {
    if (error) CPLOG_E("DB", "%s", line);
    else CPLOG_I("DB", "%s", line);
  }
  void removeFile(const char* path) override {
    ioEnter();
    remove(path);
    ioLeave();
  }
  // One SQLite file operation on the card at a time, each releasing the bus for the radio afterwards
  void ioEnter() override { if (_onSd) _sdLock.reset(new sdcard::Lock()); }
  void ioLeave() override { _sdLock.reset(); }

private:
  bool _inTask, _onSd;
  std::unique_ptr<sdcard::Lock> _sdLock;   // the firmware builds as C++11: no std::optional
};

struct DbTrial {
  DbProbeOptions options;
  char journal[12] = "truncate", sync[8] = "normal", path[32] = "/littlefs/dbtest.db";
  bool inTask = true, onSd = false;
  uint32_t rxBefore = 0, errBefore = 0;
};
static DbTrial g_trial;
static volatile bool g_trialRunning = false;

static void dbRun() {
  DbTrial& t = g_trial;
  DeviceDb host(t.inTask, t.onSd);
  DbProbe probe(host);
  DbProbeResult r;
  probe.run(t.options, r);
  if (cpadv::RadioMonitor* m = board.radioMonitor()) {
    CPLOG_I("DB", "radio during the trial: %lu packets received, %lu receive errors (%s)",
            (unsigned long)(m->received() - t.rxBefore), (unsigned long)(m->errors() - t.errBefore),
            t.inTask ? "loop() kept running" : "served between transactions only");
  }
  g_trialRunning = false;
}

static void dbTask(void*) {
  dbRun();
  vTaskDelete(nullptr);
}

// One "key=value" option of `db run`. False: unknown key.
static bool dbOption(DbTrial& t, const char* key, const char* value) {
  DbProbeOptions& o = t.options;
  if (!strcmp(key, "page")) o.pageSize = atoi(value);
  else if (!strcmp(key, "cache")) o.cacheKB = atoi(value);
  else if (!strcmp(key, "batch")) o.batch = atoi(value) > 0 ? atoi(value) : 1;
  else if (!strcmp(key, "journal")) snprintf(t.journal, sizeof(t.journal), "%s", value);
  else if (!strcmp(key, "sync")) snprintf(t.sync, sizeof(t.sync), "%s", value);
  else if (!strcmp(key, "task")) t.inTask = atoi(value) != 0;
  else if (!strcmp(key, "keep")) o.keep = atoi(value) != 0;
  else if (!strcmp(key, "where")) t.onSd = !strcmp(value, "sd");
  else return false;
  return true;
}

// db run [contacts] [messages] [page= cache= journal= sync= batch= where=flash|sd task=1|0 keep=0|1]  |  db clean
static void dbCommand(const char* a) {
  if (!strncmp(a, "clean", 5)) {
    remove("/littlefs/dbtest.db");
    remove("/littlefs/dbtest.db-journal");
    if (sdcard::mounted()) {
      sdcard::Lock lock;
      remove("/sd/dbtest.db");
      remove("/sd/dbtest.db-journal");
    }
    CPLOG_I("DB", "test databases removed");
    return;
  }
  if (strncmp(a, "run", 3) != 0) {
    CPLOG_I("DB", "usage: db run [contacts] [messages] [page=1024] [cache=16] [journal=delete|truncate|persist|memory|off] "
                  "[sync=full|normal|off] [batch=10] [where=flash|sd] [task=1|0] [keep=0|1] | db clean");
    return;
  }
  if (g_trialRunning) {
    CPLOG_W("DB", "a trial is already running");
    return;
  }
  g_trial = DbTrial();
  DbTrial& t = g_trial;
  t.options.schema = DB_SCHEMA;
  char buf[160];
  snprintf(buf, sizeof(buf), "%s", a + 3);
  int positional = 0;
  for (char* tok = strtok(buf, " "); tok; tok = strtok(nullptr, " ")) {
    char* eq = strchr(tok, '=');
    if (!eq) {
      (positional++ == 0 ? t.options.contacts : t.options.messages) = atoi(tok);
      continue;
    }
    *eq = 0;
    if (!dbOption(t, tok, eq + 1)) {
      CPLOG_W("DB", "unknown option '%s' (type: db)", tok);
      return;
    }
  }
  if (t.onSd) {
    if (!sdcard::mounted()) {
      CPLOG_W("DB", "where=sd: no SD card");
      return;
    }
    if (t.inTask) CPLOG_I("DB", "where=sd runs in loop() (task=0): the radio does not take the card's bus lock");
    t.inTask = false;
    snprintf(t.path, sizeof(t.path), "/sd/dbtest.db");
  }
  t.options.journal = t.journal;
  t.options.sync = t.sync;
  t.options.path = t.path;
  if (cpadv::RadioMonitor* m = board.radioMonitor()) {
    t.rxBefore = m->received();
    t.errBefore = m->errors();
  }
  g_trialRunning = true;
  if (!t.inTask) {
    CPLOG_I("DB", "running in loop(): the screen and keys pause, the radio is served between transactions");
    dbRun();
    return;
  }
  // 16 KB of stack: SQLite's parser and the trial's buffers. Same priority as loop(), on the same core.
  if (xTaskCreatePinnedToCore(dbTask, "dbtest", 16384, nullptr, 1, nullptr, 1) != pdPASS) {
    CPLOG_E("DB", "cannot start the trial task (heap %u, largest block %u)", ESP.getFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    g_trialRunning = false;
    return;
  }
  CPLOG_I("DB", "trial started in its own task: the device stays usable, results follow in the log");
}
#endif

static void setupConsole() {
  console.add("status", "banner + one status line", [](const char*) { cpadv::printBanner(); heartbeat(); });
  console.add("i2c", "scan the internal I2C bus (keyboard 0x34, Cap 0x43, audio 0x18)",
              [](const char*) { cpadv::scanInternalI2c(); });
  console.add("keys", "on|off: log every raw keyboard event", [](const char* a) {
    keyboard.setRawEcho(argOn(a));
    CPLOG_I("KEYS", "raw key log %s, keyboard %s, %lu events so far", argOn(a) ? "on" : "off",
            keyboard.ok() ? "ok" : "NOT RESPONDING", (unsigned long)keyboard.eventsRead());
  });
#if CPADV_DB_TEST
  console.add("db", "run [contacts] [messages] [key=value...]: SQLite trial (type 'db' for the keys) | clean", dbCommand);
#endif
#if !CPADV_DEMO
  console.add("flood", "how this node floods: path ID size and region", [](const char*) {
    const FloodPolicy& f = meshNode.floods();
    CPLOG_I("MESH", "floods: path ID %u byte(s), region %s", f.pathHashBytes(), f.scoped() ? f.region() : "none");
  });
  console.add("gps", "raw on|off: copy NMEA lines to the log; cold: cold restart", [](const char* a) {
    if (!strncmp(a, "raw", 3)) gpsDev.setRawEcho(argOn(a + (a[3] ? 4 : 3)));
    else if (!strcmp(a, "cold")) gpsDev.coldRestart();
    const GpsState& g = gpsDev.state();
    CPLOG_I("GPS", "baud %lu, %lu bytes, %lu sentences, %lu checksum errors, fix %d, %u sats, %.5f %.5f",
            (unsigned long)gpsDev.baud(), (unsigned long)gpsDev.bytesReceived(), (unsigned long)g.sentences,
            (unsigned long)g.checksumErrors, (int)g.valid, g.sats, g.lat, g.lon);
  });
#endif
  console.add("boot", "companion|standalone: restart into the other firmware (dual image)", [](const char* a) {
    if (!strcmp(a, "companion")) {
#if !CPADV_DEMO
      meshNode.flush();
#endif
      cpadv::rebootInto(cpadv::Slot::Companion);
    }
    else if (!strcmp(a, "standalone")) cpadv::rebootInto(cpadv::Slot::Standalone);
    else CPLOG_I("BOOT", "dual image %s, running %s", cpadv::dualBootImage() ? "yes" : "no",
                 cpadv::runningSlot() == cpadv::Slot::Standalone ? "standalone" : "companion");
  });
  console.add("reboot", "restart the Cardputer", [](const char*) {
    CPLOG_I("MAIN", "restart requested from the console");
    delay(100);
    ESP.restart();
  });
}

void setup() {
  board.begin();   // starts the serial log first (banner, I2C scan), then the hardware

  // Shared with phone mode in the dual image; logs usage, and why if it had to format (VALIDATION-8)
  if (!cpadv::dataFs().mount(true, "keyboard")) CPLOG_E("MAIN", "data partition unavailable: nothing will be saved");
  settings.load();
  {
    int total = (int)(cpadv::dataFs().totalBytes() / 1024);
    meshNode.messages().setCapacityFromFlash(total);
    CPLOG_I("MAIN", "storage %d KB -> up to %d conversations kept", total,
                  meshNode.messages().capacity());
  }

#if CPADV_DEMO
  gpsProxy.target = &gpsDev;
  uint32_t t = buildUnix();
  if (rtc_clock.getCurrentTime() < t) rtc_clock.setCurrentTime(t);
  gpsDev.baseUnix = t;
  DemoMesh::logSink = [](const char* line) { CPLOG_I("MAIN", "demo: %s", line); };
  meshNode.begin();
  meshNode.gps = &gpsDev;
  meshNode.enableBot(esp_random());
  meshNode.seedDemo();
  CPLOG_I("MAIN", "DEMO mode: no radio, simulated contacts and GPS");
#else
  radioReady = radio_init();
  settings.setRadioReady(radioReady);
  if (radioReady) {
    fastRng.begin(radio_driver.getRngSeed());
    meshNode.begin();
    settings.applyRadio();
    bootAdvertAt = millis() + 3000;   // let the radio settle, then announce ourselves
  }
  meshNode.setGps(&gpsDev);
  gpsDev.begin();   // the GPS UART is ours: EnvironmentSensorManager is not started
  backup.setFlushHook([] { meshNode.flush(); });
#endif

  sdcard::begin();   // after the radio: the card shares its SPI bus
#if CPADV_MAP
  sdTiles.begin();
#endif

  compassScreen.setChat(&chatScreen);
  contactDetail.setCompass(&compassScreen);
  chatScreen.setMessageDetail(&messageDetail);
  messageDetail.setRouteScreen(&routeScreen);
#if CPADV_MAP
  routeScreen.setMap(&mapScreen);
  contactDetail.setMap(&mapScreen);
#endif
  keyboard.begin();

  M5.Display.setRotation(1);
  appliedBrightness = brightnessLevel();
  M5.Display.setBrightness(appliedBrightness);
  canvas.setColorDepth(16);
  canvas.setPsram(false);
  if (!canvas.createSprite(theme::SCREEN_W, theme::SCREEN_H)) {
    CPLOG_E("MAIN", "cannot allocate the 64.8 KB framebuffer");
  }

  sys.setYieldHook(meshLoop);   // long renders (map) keep the radio served
  app.begin(&home);
#if CPADV_DEMO
  app.notify(tr("Demo mode", "Mode démo"), tr("No radio: simulated contacts and GPS", "Pas de radio : contacts et GPS simulés"), 5000);
#else
  if (!radioReady) app.notify(tr("No radio", "Radio absente"), tr("Check the LoRa Cap and its antenna", "Vérifier le Cap LoRa et l'antenne"), 8000);
#endif
  if (cpadv::dataFs().dataLostAtBoot()) {
    // The partition held data that could not be mounted (corrupted, or written by another file system):
    // identity, contacts and settings are new. The SD backup brings them back.
    app.notify(tr("Storage was reset", "Mémoire réinitialisée"),
               sdcard::mounted() ? tr("Settings > Restore from SD", "Réglages > Restaurer depuis SD")
                                 : tr("Data lost: new identity", "Données perdues : nouvelle identité"),
               10000);
  }
  lastActivity = millis();
  setupConsole();
  board.setHeartbeat(heartbeat);
  board.onBootComplete();
  CPLOG_I("MAIN", "type 'help' in the serial monitor for debug commands");
  CPLOG_I("MAIN", "ready, free heap %u, min %u, largest block %u", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                ESP.getMaxAllocHeap());
}

void loop() {
  // Radio first: never let the UI delay packet handling
  meshLoop();
  board.loop();     // boot log replay + heartbeat
  console.loop();

  KeyEvent e;
  while (keyboard.poll(e)) {
    lastActivity = millis();
    if (screenAsleep) {   // first key only wakes the screen
      wakeScreen();
      continue;
    }
    app.handleKey(e);
    meshLoop();
  }

#if !CPADV_DEMO
  gpsDev.loop();
  if (bootAdvertAt && (int32_t)(millis() - bootAdvertAt) >= 0) {
    bootAdvertAt = 0;
    meshNode.sendAdvert(true, settings.get().shareLocation);
  }
#endif

  if (screenAsleep) {
    // Screen off: no drawing at all, but events still notify (sound) and wake the screen
    if (app.pollEvents() && app.bannerVisible()) {
      lastActivity = millis();
      wakeScreen();
    }
  } else {
    if (brightnessLevel() != appliedBrightness) {   // changed in the settings
      appliedBrightness = brightnessLevel();
      M5.Display.setBrightness(appliedBrightness);
    }
    if (app.loop()) canvas.pushSprite(&M5.Display, 0, 0);
  }

  uint16_t timeout = settings.get().screenTimeoutSecs;
  if (timeout && !screenAsleep && millis() - lastActivity > timeout * 1000UL) sleepScreen();

  // Battery: one warning at 10 %, and the level in the logs for autonomy measurements
  if (millis() - lastBatteryCheck > 30000) {
    lastBatteryCheck = millis();
    int pct = M5.Power.getBatteryLevel();
    if (pct >= 0 && pct <= 10 && !lowBatteryWarned) {
      lowBatteryWarned = true;
      char t[40];
      snprintf(t, sizeof(t), tr("%d %%, please recharge", "%d %%, pense à recharger"), pct);
      wakeScreen();
      app.notify(tr("Low battery", "Batterie faible"), t, 5000);
      sys.playSound(SOUND_FAIL);
    } else if (pct > 20) {
      lowBatteryWarned = false;
    }
  }

#if !CPADV_DEMO
  if (sys.companionBootDue(millis())) {
    meshNode.flush();   // contacts and channels on flash before leaving
    cpadv::rebootInto(cpadv::Slot::Companion);   // only returns on failure
    app.notify(tr("Failed", "Échec"), tr("Phone mode not installed", "Mode téléphone non installé"), 3000);
  }
#endif

  if (backup.restartPending() && (int32_t)(millis() - backup.restartAt()) >= 0) {
    CPLOG_I("MAIN", "restarting after restore");
    Serial.flush();
    ESP.restart();
  }

  if (millis() - lastHeapLog > 60000) {
    lastHeapLog = millis();
    RadioStats rs = meshNode.radioStats();
    CPLOG_I("MAIN", "heap %u min %u big %u | bat %d%% %umV | screen %s | rx %u tx %u err %u | contacts %d convs %d",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), M5.Power.getBatteryLevel(),
                  board.getBattMilliVolts(), screenAsleep ? "off" : "on", rs.rxPackets, rs.txPackets, rs.rxErrors,
                  meshNode.contactCount(), meshNode.messages().count());
  }

  delay(screenAsleep ? 10 : 2);
}
