// Cardputer UI simulator (headless).
//
// Runs the portable UI (app/core) on a PC, with fake services,
// driven by a scenario script. Writes PNG screenshots (scaled x3) on demand.
//
// Usage: cardputer_sim <scenario.txt> [output_dir]      (scenario "-" = stdin)
//
// Scenario commands (one per line; lines starting with '#' are comments):
//   wait <ms>                      advance the virtual clock (UI loop runs every 20 ms)
//   key <k>[*N]                    one key press (N times): a char, or enter|esc|bksp|tab|up|down|left|right|space,
//                                  optional prefixes fn+ shift+ ctrl+ alt+   (e.g. key fn+;  key bksp*5)
//   type <text>                    type each character of <text> (everything after "type ", spaces included)
//   rx <from> <text...>            receive a direct message (creates the contact if unknown)
//   rxch <channel> <from> <text>   receive a message on a joined channel (e.g. Public)
//   hear <name> <snr>              a neighbour advert is heard (new contact => notification)
//   rxburst <from> <n>             receive n numbered direct messages ("message 1".."message n"), 1 s apart
//   contact <name> [hops] [fav] [repeater|room] [pos <lat> <lon>]   add a known contact (hops -1 = no path)
//   net ack on|off                 contacts acknowledge direct messages (off => retries then failure)
//   net delay <ms>                 ACK round trip;  net busy on|off: the radio refuses to send
//   reboot                         flush and reload the message store from disk, back to the home screen
//   pos <lat> <lon> | pos none     own position (contact distance / bearing)
//   tca <row> <col> [down|up]      raw Cardputer ADV key through the TCA8418 decoding (KeyMatrix),
//                                  row 0..3 / col 0..13 of the physical keyboard; default = press+release
//   name <self name>               set this node's name (without going through the settings screen)
//   lang en|fr                     UI language (English by default, like the firmware)
//   battery <pct> [charging]
//   gps fix <sats> | gps nofix | gps none   quick GPS states (none = module silent)
//   nmea <sentence>                feed one NMEA sentence to the GPS parser ("$GPGGA,...*hh")
//   nmeafile <path> [ms]           replay a recorded NMEA file, one sentence group per [ms] (default 1000)
//   expect-gps <field> <value>     fix 0/1 | sats | lat | lon (6 decimals) | speed (km/h, 1 decimal)
//   demo                           demo firmware mode: talking characters (bot) + fake walking GPS
//   sd on|off                      SD card present (backup / restore)
//   tiles <dir>                    use the map tiles of <dir>/tiles (made by tools/tiles/make_tiles.py)
//   expect-tiles-read <n>          number of tile files opened so far (render cost / caching checks)
//   nmea-bad <sentence>            the sentence must be rejected (bad checksum / malformed)
//   expect-adverts-pos <n>         number of adverts sent with the GPS position
//   ble on|off|connected
//   dual on|off                    the companion firmware is installed next to this one (dual-boot image)
//   expect-companion-boots <n>     number of "restart in phone mode" requests
//   time <unix>                    set the wall clock (UTC)
//   shot <name>                    write <output_dir>/<name>.png
//   expect-screen <title>          fail (exit 1) if the current screen title differs
//   expect-unread <n>              fail if the unread counter differs
//   expect-setting <field> <value> name|preset|tx|tz|timeout|brightness|sound|share|lang (tz in minutes, bools 0/1)
//   expect-convs <n>               number of conversations
//   expect-msgs <conv> <n>         number of stored messages in a conversation (by name)
//   expect-last <conv> <state> [text...]  state of the newest message: received|pending|sent|delivered|failed|channel
//   expect-contacts <n>            number of contacts
//   expect-sounds <n>              number of notification sounds played so far
//
// Fake hardware (real drivers of core/drivers against the fake chips of fakes/, see HwRig.h).
// With SIM_HW=1 in the environment the keyboard mode is on from the start ("make run" runs every
// scenario both ways). Then key / type / tca press physical keys of a fake TCA8418.
//   hw keyboard on|absent|present|reset|nowire   reset = chip lost its configuration (power glitch)
//   hw gps on                      the UI GPS becomes the GpsLink driver fed by a fake module
//   hw gps baud <n> | silent on|off | fix <lat> <lon> <sats> | nofix | badsum on|off
//   hw cap [absent]                probe the Cap expander and switch the antenna on (like the board)
//   expect-hw <what> <value>       keyboard 0/1 | kb-reinits n | gps-baud n | gps-present 0/1 | cap-p0 0/1
//                                  | clock-sets n
#include "../core/App.h"
#include "../core/screens/HomeScreen.h"
#include "../core/screens/SettingsScreens.h"
#include "../core/screens/MessagingScreens.h"
#include "../core/screens/GpsScreens.h"
#include "../core/screens/MapScreen.h"
#include "../core/DemoGps.h"
#include "../core/KeyMatrix.h"
#include "SimServices.h"
#include "PngWriter.h"
#include "HwRig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>

static constexpr int ZOOM = 3;
static constexpr uint32_t LOOP_MS = 20;

static LGFX_Sprite g_canvas;     // the single 240x135 RGB565 framebuffer (same as on the device)
static int g_frames = 0;
static int g_failures = 0;

static bool writePng(const std::string& path) {
  const int W = theme::SCREEN_W * ZOOM, H = theme::SCREEN_H * ZOOM;
  std::vector<uint8_t> rgb((size_t)W * H * 3);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      uint16_t c = g_canvas.readPixel(x / ZOOM, y / ZOOM);   // RGB565
      uint8_t* p = &rgb[((size_t)y * W + x) * 3];
      p[0] = ((c >> 11) & 0x1F) * 255 / 31;
      p[1] = ((c >> 5) & 0x3F) * 255 / 63;
      p[2] = (c & 0x1F) * 255 / 31;
    }
  }
  return png::write(path, W, H, rgb);
}

static bool parseKey(std::string k, KeyEvent& e) {
  e = KeyEvent();
  for (;;) {
    if (k.rfind("fn+", 0) == 0 && k.size() > 3) { e.mods |= MOD_FN; k = k.substr(3); }
    else if (k.rfind("shift+", 0) == 0 && k.size() > 6) { e.mods |= MOD_SHIFT; k = k.substr(6); }
    else if (k.rfind("ctrl+", 0) == 0 && k.size() > 5) { e.mods |= MOD_CTRL; k = k.substr(5); }
    else if (k.rfind("alt+", 0) == 0 && k.size() > 4) { e.mods |= MOD_ALT; k = k.substr(4); }
    else break;
  }
  if (k == "enter") e.code = KeyCode::Enter;
  else if (k == "esc") e.code = KeyCode::Esc;
  else if (k == "bksp") e.code = KeyCode::Backspace;
  else if (k == "tab") e.code = KeyCode::Tab;
  else if (k == "up") e.code = KeyCode::Up;
  else if (k == "down") e.code = KeyCode::Down;
  else if (k == "left") e.code = KeyCode::Left;
  else if (k == "right") e.code = KeyCode::Right;
  else if (k == "space") { e.code = KeyCode::Char; e.ch = ' '; }
  else if (k.size() == 1) {
    e.code = KeyCode::Char;
    e.ch = k[0];
    // Same translation as the hardware driver: Fn + arrow-printed keys = arrows, Fn + ` = Esc
    if (e.mods & MOD_FN) {
      KeyEvent n = toNavigation(e);
      n.mods = e.mods;
      e = n;
    } else if ((e.mods & MOD_SHIFT) && e.ch >= 'a' && e.ch <= 'z') {
      e.ch -= 32;
    }
  } else {
    return false;
  }
  return true;
}

static std::string rest(std::istringstream& in) {
  std::string r;
  std::getline(in, r);
  size_t p = r.find_first_not_of(' ');
  return p == std::string::npos ? "" : r.substr(p);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <scenario.txt|-> [output_dir]\n", argv[0]);
    return 2;
  }
  std::string outDir = argc > 2 ? argv[2] : ".";

  g_canvas.setColorDepth(16);
  if (!g_canvas.createSprite(theme::SCREEN_W, theme::SCREEN_H)) {
    fprintf(stderr, "cannot allocate framebuffer\n");
    return 2;
  }

  // Fresh storage directory per scenario: build/simfs/<scenario name>
  std::string scen = strcmp(argv[1], "-") ? argv[1] : "stdin";
  scen = scen.substr(scen.find_last_of('/') + 1);
  std::string fsDir = outDir + "/../simfs/" + scen;
  std::string cmdline = "rm -rf '" + fsDir + "' && mkdir -p '" + fsDir + "'";
  if (system(cmdline.c_str()) != 0) { fprintf(stderr, "cannot create %s\n", fsDir.c_str()); return 2; }
  SimStorage storage(fsDir);

  SimSystem sys;
  SimSettings settings;
  strcpy(settings.s.nodeName, "Cardputer-Sim");
  SimGps gpsSvc(sys);
  sys.gps = &gpsSvc;
  SimMesh mesh(sys, settings, storage);
  mesh.begin();
  mesh.gps = &gpsSvc;
  SimTiles tiles;
  SimBackup backup(settings);
  DemoGps demoGps(sys);
  bool demo = false;
  Services svc{&mesh, &sys, &settings, &gpsSvc, &tiles, &backup};
  App app(svc, g_canvas);
  KeyMatrix matrix;

  TextInputScreen textInput;
  ChatScreen chat;
  ContactDetailScreen contactDetail(chat);
  ContactsScreen contacts(chat, contactDetail);
  ConversationsScreen conversations(chat, contacts, textInput);
  CompassScreen compass;
  compass.setChat(&chat);
  contactDetail.setCompass(&compass);
  GpsScreen gps(compass);
  MapScreen map;
  SystemScreen systemScreen;
  SettingsScreen settingsScreen(textInput, systemScreen);
  HomeScreen home(&conversations, &contacts, &gps, &map, &settingsScreen);
  app.begin(&home);

  HwRig hw(sys);
  const char* simHw = getenv("SIM_HW");
  if (simHw && *simHw == '1') {
    hw.keyboardOn();
    printf("[SIM] fake hardware keyboard: keys go through the TCA8418 driver\n");
  }
  bool capP0 = false;

  auto runFor = [&](uint32_t ms) {
    uint32_t end = sys.virtualMillis + ms;
    do {
      hw.tick(sys.virtualMillis, [&](const KeyEvent& e) { app.handleKey(e); });
      if (demo) demoGps.tick();
      mesh.tick();
      if (app.loop()) g_frames++;
      if (sys.virtualMillis >= end) break;
      sys.virtualMillis += LOOP_MS;
    } while (true);
  };

  std::ifstream file;
  std::istream* in = &std::cin;
  if (strcmp(argv[1], "-") != 0) {
    file.open(argv[1]);
    if (!file) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    in = &file;
  }

  std::string line;
  int lineNo = 0;
  runFor(0);
  while (std::getline(*in, line)) {
    lineNo++;
    size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#') continue;   // blank line or comment
    std::istringstream ls(line);
    std::string cmd;
    if (!(ls >> cmd)) continue;

    if (cmd == "wait") {
      uint32_t ms = 0; ls >> ms; runFor(ms);
    } else if (cmd == "key") {
      std::string k; ls >> k;
      int times = 1;
      size_t star = k.rfind('*');
      if (star != std::string::npos && star > 0 && star + 1 < k.size()) { times = atoi(k.c_str() + star + 1); k = k.substr(0, star); }
      KeyEvent e;
      if (!parseKey(k, e)) { fprintf(stderr, "line %d: unknown key '%s'\n", lineNo, k.c_str()); g_failures++; continue; }
      for (int i = 0; i < times; i++) {
        if (hw.keyboardEnabled()) {
          if (!hw.tapKey(e)) { fprintf(stderr, "line %d: no physical key for '%s'\n", lineNo, k.c_str()); g_failures++; break; }
        } else {
          app.handleKey(e);
        }
        runFor(LOOP_MS);
      }
    } else if (cmd == "type") {
      std::string t = line.substr(line.find("type") + 4);
      if (!t.empty() && t[0] == ' ') t.erase(0, 1);
      for (char c : t) {
        if (hw.keyboardEnabled()) {
          if (!hw.tapChar(c)) { fprintf(stderr, "line %d: no physical key for '%c'\n", lineNo, c); g_failures++; }
        } else {
          KeyEvent e; e.code = KeyCode::Char; e.ch = c; app.handleKey(e);
        }
        runFor(LOOP_MS);
      }
    } else if (cmd == "rx") {
      std::string from; ls >> from; mesh.receive(from, rest(ls)); runFor(LOOP_MS);
    } else if (cmd == "rxch") {
      std::string ch, from; ls >> ch >> from; mesh.receiveChannel(ch, from, rest(ls)); runFor(LOOP_MS);
    } else if (cmd == "rxburst") {
      std::string from; int n = 0; ls >> from >> n;
      for (int i = 1; i <= n; i++) { mesh.receive(from, "message " + std::to_string(i)); runFor(1000); }
    } else if (cmd == "contact") {
      std::string name, w; ls >> name;
      int hops = 1; bool fav = false, hasPos = false; uint8_t type = CONTACT_CHAT; double lat = 0, lon = 0;
      if (ls >> hops) {
        while (ls >> w) {
          if (w == "fav") fav = true;
          else if (w == "repeater") type = CONTACT_REPEATER;
          else if (w == "room") type = CONTACT_ROOM;
          else if (w == "pos") { ls >> lat >> lon; hasPos = true; }
        }
      }
      mesh.addContact(name, hops, fav, type, hasPos, lat, lon); app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "net") {
      std::string what, v; ls >> what >> v;
      if (what == "ack") mesh.netAcks = (v == "on");
      else if (what == "delay") mesh.netDelayMs = atoi(v.c_str());
      else if (what == "busy") mesh.radioBusy = (v == "on");
    } else if (cmd == "reboot") {
      mesh.reboot(); app.begin(&home); runFor(LOOP_MS);
    } else if (cmd == "pos") {
      std::string a; ls >> a;
      if (a == "none") sys.st.hasPosition = false;
      else { sys.st.lat = atof(a.c_str()); ls >> sys.st.lon; sys.st.hasPosition = true; }
      app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "hear") {
      std::string n; float snr = 0; ls >> n >> snr; mesh.hear(n, snr); runFor(LOOP_MS);
    } else if (cmd == "tca") {
      int r = -1, c = -1; std::string mode; ls >> r >> c >> mode;
      uint8_t k = KeyMatrix::fromGrid(r, c);
      if (!k) { fprintf(stderr, "line %d: no key at %d,%d\n", lineNo, r, c); g_failures++; continue; }
      if (hw.keyboardEnabled()) {
        if (mode != "up") hw.kbChip.pressGrid(r, c, true);
        if (mode != "down") hw.kbChip.pressGrid(r, c, false);
      } else {
        KeyEvent e;
        if (mode != "up" && matrix.feed(0x80 | k, e)) app.handleKey(e);
        if (mode != "down" && matrix.feed(k, e)) app.handleKey(e);
      }
      runFor(LOOP_MS);
    } else if (cmd == "hw") {
      std::string what, a; ls >> what >> a;
      if (what == "keyboard") {
        if (a == "on") hw.keyboardOn();
        else if (a == "absent") hw.kbChip.absent = true;
        else if (a == "present") hw.kbChip.absent = false;
        else if (a == "nowire") hw.kbChip.intWired = false;
        else if (a == "reset") { hw.kbChip.regs.assign(0x30, 0); hw.kbChip.fifo.clear(); }
        else { fprintf(stderr, "line %d: hw keyboard on|absent|present|reset|nowire\n", lineNo); g_failures++; }
      } else if (what == "gps") {
        if (a == "on") { GpsService* g = hw.gpsOn(); sys.gps = g; mesh.gps = g; app.services().gps = g; }
        else if (a == "baud") ls >> hw.gpsUart.moduleBaud;
        else if (a == "silent") { std::string v; ls >> v; hw.gpsUart.silent = (v == "on"); }
        else if (a == "badsum") { std::string v; ls >> v; hw.gpsUart.corruptChecksums = (v == "on"); }
        else if (a == "fix") { double la = 0, lo = 0; int n = 0; ls >> la >> lo >> n; hw.gpsModuleFix(la, lo, n); }
        else if (a == "nofix") hw.gpsModuleNoFix();
        else { fprintf(stderr, "line %d: hw gps on|baud|silent|badsum|fix|nofix\n", lineNo); g_failures++; }
      } else if (what == "cap") {
        hw.capChip.absent = (a == "absent");
        capP0 = hw.capBegin();
      } else {
        fprintf(stderr, "line %d: hw keyboard|gps|cap\n", lineNo); g_failures++;
      }
      app.invalidate();
      runFor(LOOP_MS);
    } else if (cmd == "expect-hw") {
      std::string what; long want = 0; ls >> what >> want;
      long got = -1;
      if (what == "keyboard") got = hw.kb.ok();
      else if (what == "kb-reinits") got = hw.kb.reinits();
      else if (what == "gps-baud") got = hw.gps.baud();
      else if (what == "gps-present") got = hw.gps.present(sys.virtualMillis);
      else if (what == "cap-p0") got = capP0 && hw.capChip.drives(0, true);
      else if (what == "clock-sets") got = hw.gps.clockSets();
      if (got != want) { fprintf(stderr, "line %d: FAIL hw %s expected %ld, got %ld\n", lineNo, what.c_str(), want, got); g_failures++; }
    } else if (cmd == "lang") {
      std::string l; ls >> l;
      UiSettings s = settings.get();
      if (l == "en") s.language = LANG_EN;
      else if (l == "fr") s.language = LANG_FR;
      else { fprintf(stderr, "line %d: lang en|fr\n", lineNo); g_failures++; continue; }
      settings.save(s); app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "name") {
      UiSettings s = settings.get(); std::string n = rest(ls);
      strncpy(s.nodeName, n.c_str(), sizeof(s.nodeName) - 1); s.nodeName[sizeof(s.nodeName) - 1] = 0;
      settings.save(s); app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "battery") {
      std::string c; ls >> sys.st.batteryPercent >> c; sys.st.charging = (c == "charging"); app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "gps") {
      std::string m; ls >> m;
      int sats = 0;
      if (m == "fix") ls >> sats;
      gpsSvc.quick(m, sats);
      app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "nmea") {
      std::string s = rest(ls);
      if (!gpsSvc.feed(s)) { fprintf(stderr, "line %d: NMEA sentence rejected: %s\n", lineNo, s.c_str()); g_failures++; }
      runFor(LOOP_MS);
    } else if (cmd == "nmeafile") {
      std::string path; uint32_t ms = 1000; ls >> path >> ms;
      if (!ms) ms = 1000;
      std::ifstream nf(path);
      if (!nf) { fprintf(stderr, "line %d: cannot open %s\n", lineNo, path.c_str()); g_failures++; continue; }
      std::string s; int count = 0;
      while (std::getline(nf, s)) {
        if (s.empty()) continue;
        gpsSvc.feed(s);
        // a new second starts with RMC: advance the clock between groups
        if (s.compare(3, 3, "RMC") == 0) runFor(ms);
        count++;
      }
      printf("[SIM] replayed %d NMEA sentences from %s\n", count, path.c_str());
    } else if (cmd == "demo") {
      demo = true;
      demoGps.baseUnix = sys.nowUnix() ? sys.nowUnix() : demoGps.baseUnix;
      sys.gps = &demoGps;
      mesh.gps = &demoGps;
      app.services().gps = &demoGps;
      mesh.enableBot(42);
      mesh.seedDemo();
      runFor(LOOP_MS);
    } else if (cmd == "sd") {
      std::string v; ls >> v; backup.card = (v == "on"); app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "tiles") {
      std::string d; ls >> d;
      if (!tiles.open(d)) { fprintf(stderr, "line %d: no tiles in %s\n", lineNo, d.c_str()); g_failures++; }
    } else if (cmd == "expect-tiles-read") {
      int want = 0; ls >> want;
      if (want != tiles.tilesRead) { fprintf(stderr, "line %d: FAIL expected %d tile reads, got %d\n", lineNo, want, tiles.tilesRead); g_failures++; }
    } else if (cmd == "nmea-bad") {
      std::string s = rest(ls);
      if (gpsSvc.feed(s)) { fprintf(stderr, "line %d: FAIL NMEA sentence accepted: %s\n", lineNo, s.c_str()); g_failures++; }
    } else if (cmd == "expect-adverts-pos") {
      int want = 0; ls >> want;
      if (want != mesh.advertsWithPosition) { fprintf(stderr, "line %d: FAIL expected %d adverts with position, got %d\n", lineNo, want, mesh.advertsWithPosition); g_failures++; }
    } else if (cmd == "expect-gps") {
      std::string field, want; ls >> field >> want;
      const GpsState& g = app.services().gps->state();
      char got[32];
      if (field == "fix") snprintf(got, sizeof(got), "%d", g.valid ? 1 : 0);
      else if (field == "sats") snprintf(got, sizeof(got), "%u", g.sats);
      else if (field == "lat") snprintf(got, sizeof(got), "%.6f", g.lat);
      else if (field == "lon") snprintf(got, sizeof(got), "%.6f", g.lon);
      else if (field == "speed") snprintf(got, sizeof(got), "%.1f", g.speedKmh);
      else snprintf(got, sizeof(got), "<unknown>");
      if (want != got) { fprintf(stderr, "line %d: FAIL gps %s expected %s, got %s\n", lineNo, field.c_str(), want.c_str(), got); g_failures++; }
    } else if (cmd == "dual") {
      std::string v; ls >> v; sys.dualBoot = (v == "on"); app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "expect-companion-boots") {
      int want = 0; ls >> want;
      if (want != sys.companionBoots) { fprintf(stderr, "line %d: FAIL expected %d companion boots, got %d\n", lineNo, want, sys.companionBoots); g_failures++; }
    } else if (cmd == "ble") {
      std::string m; ls >> m;
      sys.st.bleEnabled = (m != "off"); sys.st.bleConnected = (m == "connected");
      app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "time") {
      uint32_t t; ls >> t; sys.st.unixTime = t - sys.virtualMillis / 1000; app.invalidate(); runFor(LOOP_MS);
    } else if (cmd == "shot") {
      std::string n; ls >> n;
      app.drawAll();
      std::string path = outDir + "/" + n + ".png";
      if (writePng(path)) printf("[SIM] screenshot %s\n", path.c_str());
      else { fprintf(stderr, "line %d: cannot write %s\n", lineNo, path.c_str()); g_failures++; }
    } else if (cmd == "expect-screen") {
      std::string want = rest(ls);
      const char* got = app.top() ? app.top()->title() : "";
      if (want != got) { fprintf(stderr, "line %d: FAIL expected screen '%s', got '%s'\n", lineNo, want.c_str(), got); g_failures++; }
    } else if (cmd == "expect-unread") {
      int want = 0; ls >> want;
      int got = mesh.unreadCount();
      if (want != got) { fprintf(stderr, "line %d: FAIL expected %d unread, got %d\n", lineNo, want, got); g_failures++; }
    } else if (cmd == "expect-setting") {
      std::string field; ls >> field; std::string want = rest(ls);
      const UiSettings& s = settings.get();
      std::string got;
      if (field == "name") got = s.nodeName;
      else if (field == "preset") got = std::to_string(s.radioPreset);
      else if (field == "tx") got = std::to_string(s.txPowerDbm);
      else if (field == "tz") got = std::to_string(s.utcOffsetMinutes);
      else if (field == "timeout") got = std::to_string(s.screenTimeoutSecs);
      else if (field == "sound") got = std::to_string(s.sound);
      else if (field == "ble") got = std::to_string(s.ble);
      else if (field == "share") got = std::to_string(s.shareLocation);
      else if (field == "brightness") got = std::to_string(s.brightness);
      else if (field == "lang") got = s.language == LANG_FR ? "fr" : "en";
      else got = "<unknown field>";
      if (want != got) { fprintf(stderr, "line %d: FAIL setting %s expected '%s', got '%s'\n", lineNo, field.c_str(), want.c_str(), got.c_str()); g_failures++; }
    } else if (cmd == "expect-convs") {
      int want = 0; ls >> want;
      if (want != mesh.messages().count()) { fprintf(stderr, "line %d: FAIL expected %d conversations, got %d\n", lineNo, want, mesh.messages().count()); g_failures++; }
    } else if (cmd == "expect-contacts") {
      int want = 0; ls >> want;
      if (want != mesh.contactCount()) { fprintf(stderr, "line %d: FAIL expected %d contacts, got %d\n", lineNo, want, mesh.contactCount()); g_failures++; }
    } else if (cmd == "expect-sounds") {
      int want = 0; ls >> want;
      if (want != sys.sounds) { fprintf(stderr, "line %d: FAIL expected %d sounds, got %d\n", lineNo, want, sys.sounds); g_failures++; }
    } else if (cmd == "expect-msgs") {
      std::string conv; int want = 0; ls >> conv >> want;
      ConvKey k = mesh.keyByName(conv);
      int got = k.valid() ? (int)mesh.messages().total(k) : -1;
      if (want != got) { fprintf(stderr, "line %d: FAIL expected %d messages in %s, got %d\n", lineNo, want, conv.c_str(), got); g_failures++; }
    } else if (cmd == "expect-last") {
      std::string conv, state; ls >> conv >> state; std::string text = rest(ls);
      static const char* const names[] = {"received", "pending", "sent", "sent", "delivered", "failed", "channel"};
      ConvKey k = mesh.keyByName(conv);
      StoredMsg m;
      if (!k.valid() || mesh.messages().loadRecent(k, &m, 1, 0) != 1) {
        fprintf(stderr, "line %d: FAIL no message in %s\n", lineNo, conv.c_str()); g_failures++;
      } else {
        std::string got = m.state < 7 ? names[m.state] : "?";
        if (got != state || (!text.empty() && text != m.text)) {
          fprintf(stderr, "line %d: FAIL last of %s: expected %s '%s', got %s '%s'\n", lineNo, conv.c_str(), state.c_str(), text.c_str(), got.c_str(), m.text);
          g_failures++;
        }
      }
    } else {
      fprintf(stderr, "line %d: unknown command '%s'\n", lineNo, cmd.c_str());
      g_failures++;
    }
  }

  printf("[SIM] done: %u ms simulated, %d frames drawn, %d failure(s)\n", sys.virtualMillis, g_frames, g_failures);
  return g_failures ? 1 : 0;
}
