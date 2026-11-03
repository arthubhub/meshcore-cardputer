#pragma once
// Fake MeshCore network, portable (std C++ only). Used by:
//  - the PC simulator, driven step by step by the scenarios (deterministic, bot off);
//  - the Cardputer demo firmware (no LoRa Cap needed): the bot makes characters talk.
// Messages go through the real MessageStore, so history and persistence are the real ones.
#include "Services.h"
#include <deque>
#include <string>
#include <vector>

class DemoMesh : public MeshService {
public:
  DemoMesh(SystemService& sys, SettingsService& settings, Storage& storage);

  void begin();               // loads the message history (storage must be ready)

  // ---- scenario hooks ----
  void addContact(const std::string& name, int hops, bool fav, uint8_t type, bool hasPos, double lat, double lon);
  void receive(const std::string& sender, const std::string& text);                           // direct
  void receiveChannel(const std::string& channel, const std::string& sender, const std::string& text);
  void hear(const std::string& name, float snr);
  void reboot();              // flush + reload the message store from the files
  void tick();                // advances the fake network (ACKs, retries, demo bot)
  // Demo firmware: characters chat on Public, reply to direct messages, adverts... (off in tests)
  void enableBot(uint32_t seed);
  void seedDemo();            // first boot of the demo: contacts and a few messages
  static void (*logSink)(const char* line);   // where logf() writes (printf by default)
  bool netAcks = true;        // contacts acknowledge direct messages
  uint32_t netDelayMs = 1500; // ACK round trip
  bool radioBusy = false;     // sendText fails (radio queue full)
  int sentPackets = 0;
  ConvKey keyByName(const std::string& name);   // conversation or contact by name

  // ---- MeshService ----
  const char* selfName() override { return _settings.get().nodeName; }
  MessageStore& messages() override { return _store; }
  bool sendText(const ConvKey& to, const char* text) override;
  int maxTextLen(const ConvKey& to) override;
  int contactCount() override { return (int)_contacts.size(); }
  bool contactAt(int i, ContactView& out) override;
  bool findContact(const ConvKey& key, ContactView& out) override;
  bool setFavourite(const ConvKey& key, bool fav) override;
  bool removeContact(const ConvKey& key) override;
  bool resetPath(const ConvKey& key) override;
  bool sendAdvert(bool flood, bool withPosition) override;
  GpsService* gps = nullptr;
  int advertsWithPosition = 0;
  bool joinChannel(const char* name) override;
  bool leaveChannel(const ConvKey& key) override;
  bool pollEvent(MeshEvent& out) override;
  int neighbours(NeighbourInfo* out, int max) override;
  RadioStats radioStats() override;

private:
  void logf(const char* fmt, ...);
  uint32_t nowUnix() { return _sys.status().unixTime; }
  uint32_t rnd();
  void botTick();
  void scheduleReply(const ConvKey& to, const std::string& text);
  struct Reply { ConvKey key; uint32_t at; std::string text; };
  bool _bot = false;
  uint32_t _rng = 1;
  uint32_t _nextChatter = 0;
  std::vector<Reply> _replies;
  struct Contact { ContactView v; uint32_t heardAtMs; float snr; };
  struct Pending { ConvKey key; uint32_t seq; uint32_t deadline; uint32_t ackAt; };
  struct Channel { ConvKey key; std::string name; };

  Contact* findMut(const ConvKey& k);
  void sortContacts();
  void push(MeshEvent::Type t, const ConvKey& k, const std::string& title, const std::string& text);
  void transmit(Pending& p, StoredMsg& m);

  SystemService& _sys;
  SettingsService& _settings;
  MessageStore _store;
  Storage& _storage;
  std::vector<Contact> _contacts;
  std::vector<Channel> _channels;
  std::vector<Pending> _pending;
  std::deque<MeshEvent> _events;
  uint32_t _rx = 0;
};
