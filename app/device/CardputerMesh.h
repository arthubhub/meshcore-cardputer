#pragma once
// MeshCore chat node for the standalone Cardputer firmware.
// BaseChatMesh does the networking (unchanged MeshCore code); this class adds what the
// phone app normally does: message history, delivery tracking with retries, contacts and
// channels persistence, and exposes it all to the UI through MeshService.
#include <Arduino.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>
#include "../core/Services.h"
#include "DeviceServices.h"
#include "LittleFsStorage.h"

class CardputerMesh : public BaseChatMesh, public MeshService {
public:
  CardputerMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables,
                DeviceSettings& settings);

  void begin();   // identity, contacts, channels, message index (LittleFS must be mounted)
  void loop();    // mesh + retries + deferred saves; call as often as possible
  bool started() const { return _started; }
  void flush() { if (_started) { saveContacts(); _store.flush(); } }   // before a backup
  void setGps(GpsService* gps) { _gps = gps; }

  // ---- MeshService ----
  const char* selfName() override { return _settings.get().nodeName; }
  MessageStore& messages() override { return _store; }
  bool sendText(const ConvKey& to, const char* text) override;
  int maxTextLen(const ConvKey& to) override;
  int contactCount() override { return getNumContacts(); }
  bool contactAt(int i, ContactView& out) override;
  bool findContact(const ConvKey& key, ContactView& out) override;
  bool setFavourite(const ConvKey& key, bool fav) override;
  bool removeContact(const ConvKey& key) override;
  bool resetPath(const ConvKey& key) override;
  bool sendAdvert(bool flood, bool withPosition) override;
  bool joinChannel(const char* name) override;
  bool leaveChannel(const ConvKey& key) override;
  bool pollEvent(MeshEvent& out) override;
  int neighbours(NeighbourInfo* out, int max) override;
  RadioStats radioStats() override;

protected:
  // ---- BaseChatMesh / Mesh ----
  float getAirtimeBudgetFactor() const override { return 1.0f; }
  int calcRxDelay(float score, uint32_t air_time) const override { return 0; }
  bool allowPacketForward(const mesh::Packet* packet) override { return false; }   // client, not a repeater
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data,
                    size_t app_data_len) override;
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo& contact) override;
  ContactInfo* processAck(const uint8_t* data) override;
  void onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override;
  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onCLICommandRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*, char*) override {}
  void onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp,
                           const uint8_t* sender_prefix, const char* text) override;
  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override {}   // timeouts are tracked per message in loop()
  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp,
                            const char* text) override;
  uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t, uint8_t*) override { return 0; }
  void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override {}

private:
  static constexpr int MAX_PENDING = 8;
  static constexpr int MAX_ATTEMPTS = 3;
  static constexpr int MAX_EVENTS = 8;
  static constexpr int MAX_NEIGHBOURS = 12;
  static constexpr int DEDUP = 12;

  struct Pending {
    ConvKey key;
    uint32_t seq;
    uint32_t ts;              // sender timestamp, identical for every attempt
    uint32_t acks[MAX_ATTEMPTS];
    uint32_t deadline;        // millis
    uint8_t attempts;
    bool used;
  };
  struct Neighbour { uint8_t prefix[6]; int8_t snrX4; uint32_t heardMs; };

  static ConvKey directKey(const uint8_t* pubKey);
  static ConvKey channelKey(const mesh::GroupChannel& ch);
  ContactInfo* contactFor(const ConvKey& key);
  int channelIndexFor(const ConvKey& key);
  void toView(const ContactInfo& c, ContactView& out);
  void storeIncoming(const ContactInfo& from, mesh::Packet* pkt, uint32_t senderTs, const char* text);
  bool isDuplicate(const uint8_t* prefix, uint32_t ts, const char* text);
  bool transmit(Pending& p, StoredMsg& m, ContactInfo& c);
  void checkPending();
  void pushEvent(MeshEvent::Type t, const ConvKey& k, const char* title, const char* text);

  void loadIdentity();
  void loadContacts();
  void saveContacts();
  void loadChannels();
  void saveChannels();
  bool addChannelSecret(const char* name, const uint8_t* secret, int len);
  void rebuildContactOrder();

  DeviceSettings& _settings;
  GpsService* _gps = nullptr;
  LittleFsStorage _storage;
  MessageStore _store;
  bool _started = false;

  Pending _pending[MAX_PENDING] = {};
  MeshEvent _events[MAX_EVENTS];
  uint8_t _evHead = 0, _evCount = 0;
  Neighbour _nbs[MAX_NEIGHBOURS] = {};
  struct { uint32_t key; uint32_t hash; } _dedup[DEDUP] = {};
  uint8_t _dedupPos = 0;

  uint16_t _order[MAX_CONTACTS] = {};   // contact slots sorted for the UI
  int _orderCount = 0;
  bool _orderDirty = true;
  bool _contactsDirty = false;
  uint32_t _contactsDirtySince = 0;
  uint32_t _lastFlush = 0;
};
