#pragma once
// The only boundary between the UI and the rest of the firmware.
// - Hardware build: implemented on top of MeshCore (BaseChatMesh), the GPS UART, the battery ADC...
// - Simulator build: implemented by sim/SimServices.* (fake mesh, NMEA replay, virtual clock).
// The UI must never include Arduino, RadioLib or MeshCore headers directly.
#include <stdint.h>
#include <stddef.h>
#include "MessageStore.h"
#include "Nmea.h"
#include "drivers/AirtimeMeter.h"
#include "drivers/ContactPosition.h"
#include "drivers/FloodOptions.h"
#include "drivers/NodeDiscovery.h"
#include "drivers/PacketLog.h"

struct DeviceStatus {
  int  batteryPercent = -1;   // -1 = unknown
  bool charging = false;
  bool gpsFix = false;
  int  gpsSatellites = 0;
  bool bleEnabled = false;
  bool bleConnected = false;
  uint32_t unixTime = 0;      // 0 = time not known yet (no GPS fix, no phone sync)
  bool hasPosition = false;   // own position (GPS fix or manual)
  double lat = 0, lon = 0;
};

struct RadioStats {
  uint32_t rxPackets = 0;
  uint32_t txPackets = 0;
  uint32_t rxErrors = 0;
  float lastRssi = 0;
  float lastSnr = 0;
  float freqMHz = 0;
  float bandwidthKHz = 0;
  uint8_t spreadingFactor = 0;
  int8_t txPowerDbm = 0;
  uint32_t txAirMsHour = 0;     // transmit time over the last hour
  float txAirPercent = 0;       // the same as a share of the hour
  float txAirLimitPercent = 0;  // duty cycle of this frequency, 0 = no limit known
};

struct NeighbourInfo {
  char name[32];
  float snr;
  uint32_t lastHeardSecs;     // seconds since last heard
};

enum ContactType : uint8_t { CONTACT_CHAT = 1, CONTACT_REPEATER = 2, CONTACT_ROOM = 3, CONTACT_SENSOR = 4 };

struct ContactView {
  ConvKey key;                // DIRECT + public key prefix: also the conversation key
  uint8_t pubKey[32];
  char name[32];
  uint8_t type;               // ContactType
  bool favourite;
  int8_t hops;                // -1: no known path (messages are flooded)
  MsgRoute route;             // the route to this contact, first repeater first
  uint32_t lastHeard;         // unix time (our clock) of the last advert, 0 = never
  bool hasPos;
  int32_t latE6, lonE6;       // position from the contact's advert or message (degrees * 1e6)
  uint32_t posTime;           // when the contact was there, by its clock (unix), 0 unknown
  uint8_t posSource;          // PositionSource
};

struct MeshEvent {
  enum Type : uint8_t { NONE = 0, MESSAGE, DELIVERED, FAILED, CONTACT_NEW, AIRTIME };
  Type type = NONE;
  ConvKey key;
  char title[32] = {};        // conversation / contact name
  char text[64] = {};         // message preview (AIRTIME: "8.1 % / 10 %")
};

// The known nodes, favourites first, then most recently heard. What looking a node up needs from the mesh.
class NodeDirectory {
public:
  virtual ~NodeDirectory() {}
  virtual int contactCount() = 0;
  virtual bool contactAt(int i, ContactView& out) = 0;
};

class MeshService : public NodeDirectory {
public:
  virtual const char* selfName() = 0;

  // Conversations and history (read by the screens, written by the mesh).
  virtual MessageStore& messages() = 0;
  int unreadCount() { return messages().unreadTotal(); }
  // Stores the message and sends it (direct with ACK + retries, or to a channel).
  virtual bool sendText(const ConvKey& to, const char* text) = 0;
  // Max text length in bytes for this conversation (channels: name prefix included).
  virtual int maxTextLen(const ConvKey& to) = 0;

  virtual bool findContact(const ConvKey& key, ContactView& out) = 0;
  virtual bool setFavourite(const ConvKey& key, bool fav) = 0;
  virtual bool removeContact(const ConvKey& key) = 0;
  virtual bool resetPath(const ConvKey& key) = 0;

  // withPosition: include the GPS position (only if there is a fix).
  virtual bool sendAdvert(bool flood, bool withPosition) = 0;
  // Joins a hashtag channel ("#name", key derived from the name, like the MeshCore app).
  virtual bool joinChannel(const char* name) = 0;
  virtual bool leaveChannel(const ConvKey& key) = 0;

  // Events for notifications (new message, delivery...). Returns false when empty.
  virtual bool pollEvent(MeshEvent& out) = 0;

  virtual int neighbours(NeighbourInfo* out, int max) = 0;
  virtual RadioStats radioStats() = 0;

  // Search for the repeaters within direct radio range. False: radio unavailable or too soon after the last
  // search (see discovery().secondsUntilNext). Answers appear in discovery() as they arrive.
  virtual bool startDiscovery() = 0;
  virtual const NodeDiscovery& discovery() const = 0;

  // Last packets heard and sent (RADIO-6); always recording. With the capture on, they also go to a CSV file on the
  // SD card. setPacketCapture(true) is false when that file cannot be opened.
  virtual const PacketLog& packetLog() const = 0;
  virtual void clearPacketLog() = 0;
  virtual bool setPacketCapture(bool on) = 0;
  virtual bool packetCapture() const = 0;
};

enum SoundKind : uint8_t { SOUND_DIRECT_MSG, SOUND_CHANNEL_MSG, SOUND_SENT, SOUND_FAIL };

class SystemService {
public:
  virtual ~SystemService() {}
  virtual uint32_t millis() = 0;
  virtual DeviceStatus status() = 0;
  virtual uint32_t freeHeap() = 0;
  virtual uint32_t minFreeHeap() = 0;          // lowest free heap since boot
  virtual uint16_t batteryMilliVolts() = 0;
  virtual const char* firmwareVersion() = 0;
  virtual void beep(uint16_t freqHz, uint16_t durationMs) = 0;
  // Short notification jingles (respect the "Son" setting).
  virtual void playSound(SoundKind kind) = 0;
  // Called by long UI operations (map rendering) so the radio keeps being served.
  virtual void yield() {}
  // Dual-boot image (this firmware + the official MeshCore companion for the phone app):
  // true when the companion is installed next to us; the request restarts into it shortly after.
  virtual bool companionInstalled() { return false; }
  virtual void requestCompanionBoot() {}
};

// User settings edited on the device. Persisted by the platform (LittleFS on the Cardputer).
struct UiSettings {
  char nodeName[32] = "Cardputer";
  uint8_t radioPreset = 0;          // index in RADIO_PRESETS (core/RadioPresets.h)
  int8_t txPowerDbm = 22;
  int16_t utcOffsetMinutes = 60;    // France: +60 in winter, +120 in summer
  uint16_t screenTimeoutSecs = 60;  // 0 = never
  bool sound = true;
  bool ble = false;                 // unused by the standalone firmware (Bluetooth = companion firmware)
  bool shareLocation = false;       // include the GPS position in adverts
  uint8_t brightness = 1;           // 0 low, 1 medium, 2 high
  uint8_t language = 0;             // LANG_EN / LANG_FR (core/I18n.h); English by default
  // Fields are only ever added at the end (DeviceSettings::load keeps the defaults of what an older file lacks).
  // reserved0 fills what was the padding byte of the older, 44-byte layout: files may hold anything there.
  uint8_t reserved0 = 0;
  uint8_t pathHashBytes = 1;        // bytes of each repeater ID in a flood's path (floodopt, RADIO-9)
  char region[31] = "";             // flood scope, "" = none (floodopt, RADIO-10)
};

class SettingsService {
public:
  virtual ~SettingsService() {}
  virtual const UiSettings& get() = 0;
  // Stores and applies the new settings (radio parameters, name...). Returns false on error.
  virtual bool save(const UiSettings& s) = 0;
};

class GpsService {
public:
  virtual ~GpsService() {}
  virtual const GpsState& state() = 0;
  virtual bool present() = 0;          // NMEA sentences received (module wired and talking)
  virtual uint32_t baud() = 0;         // detected baud rate, 0 while searching
  virtual void coldRestart() = 0;      // forget the almanac / last position
};

// Offline raster map tiles (256x256, big-endian RGB565, "/tiles/<z>/<x>/<y>.bin" on the SD card).
class TileSource {
public:
  virtual ~TileSource() {}
  virtual bool available() = 0;                  // card mounted and tiles found
  virtual bool zoomRange(int& zmin, int& zmax) = 0;
  // Default view of the tile set (tiles/info.txt), used before the first GPS fix.
  virtual bool hint(double& lat, double& lon) = 0;
  virtual bool beginTile(int z, int x, int y) = 0;    // false if the tile is missing
  // Reads `w` pixels of row `row` starting at column `x0` of the open tile.
  virtual bool readRow(int row, int x0, int w, uint16_t* dst) = 0;
  virtual void endTile() = 0;
};

// Configuration backup on the SD card: settings, identity (private key!), contacts, channels.
class BackupService {
public:
  virtual ~BackupService() {}
  virtual bool available() = 0;                          // SD card mounted
  virtual bool exportConfig(char* msg, size_t n) = 0;    // msg: short result for the user
  // On success the device restarts a moment later to load the restored configuration.
  virtual bool importConfig(char* msg, size_t n) = 0;
};

struct Services {
  MeshService* mesh;
  SystemService* sys;
  SettingsService* settings;
  GpsService* gps;
  TileSource* tiles;    // nullptr when the firmware is built without the map
  BackupService* backup;
};
