#pragma once
// MeshCore's chat node with the node archive behind its contact table (MESSAGING-12, step 5; docs/storage/NODES.md).
//
// The bug it fixes: the contact table holds 100 nodes (RAM). On a busy mesh the adverts of repeaters filled it, and
// every new advert replaced the contact heard the longest time ago (VALIDATION-11): a friend who had not advertised
// for a while left the table, and from then on their messages could not be decrypted and ours could not be sent.
//
// With an archive attached (attachArchive):
//  - every advert heard goes to the archive (all node types), with its signal and hop count;
//  - only people enter the contact table by themselves (shouldAutoAddContactType); repeaters, rooms and sensors stay
//    in the archive, where routes still find their names and positions;
//  - a packet for us from a node outside the table: MeshCore asks for the shared secret of each candidate with the
//    sender's ID until one decrypts. The table's candidates come first (BaseChatMesh); after them this class offers
//    up to archiveCandidates() archive nodes, looked up only when the table's all failed, so a message from a contact
//    costs nothing more. The node whose secret decrypts the packet comes back into the table (a place is freed by the
//    VALIDATION-11 rule), and the packet then goes through BaseChatMesh as for any contact: message, ACK, route;
//  - ECDH budget: at most ECDH_PER_MINUTE computations for archive candidates, against forged packets;
//  - bringBack() puts an archived node back into the table (sending to a conversation whose contact left).
// Secrets are computed when needed, kept in RAM only for the packet being decrypted, never stored.
// Without an archive it behaves exactly like BaseChatMesh. Portable on MeshCore: the wire tests run it on the PC.
#include <helpers/BaseChatMesh.h>
#include "../core/drivers/NodeBook.h"

class ArchivingChatMesh : public BaseChatMesh {
public:
  static constexpr int MAX_CANDIDATES = 8;
  static constexpr int ECDH_PER_MINUTE = 32;

  struct ArchiveStats {
    uint32_t adverts = 0;          // hearings sent to the archive
    uint32_t lookups = 0;          // senders looked up in the archive
    uint32_t ecdh = 0;             // shared secrets computed for archive candidates
    uint32_t ecdhRefused = 0;      // candidates skipped: budget spent
    uint32_t broughtBack = 0;      // archive nodes put back into the table
  };

  void attachArchive(NodeBook* book) { _book = book; }
  NodeBook* archive() const { return _book; }
  const ArchiveStats& archiveStats() const { return _ast; }

  // The node of `key` (32 bytes, or a prefix of `keyLen` bytes that only one archived node has) from the archive back
  // into the table. Returns the contact, nullptr when unknown or the table refused (full of favourites).
  ContactInfo* bringBack(const uint8_t* key, int keyLen = PUB_KEY_SIZE);
  // Records a contact of the table in the archive (boot, changes of the UI): flags follow the table.
  void archiveContact(const ContactInfo& c, bool inTable);

protected:
  ArchivingChatMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
                    mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : BaseChatMesh(radio, ms, rng, rtc, mgr, tables) {}

  // ---- policy, for subclasses
  // Which node types enter the table by themselves (with an archive): people only by default
  virtual bool keepInTable(uint8_t type) const { return type == ADV_TYPE_CHAT; }
  // Archive candidates tried for one packet (CardputerMesh: fewer at 80 MHz, ECDH takes 43 ms there)
  virtual int archiveCandidates() const { return MAX_CANDIDATES; }
  virtual int lastRssi() const { return 0; }
  virtual uint32_t nowMs() const = 0;
  // A node came back from the archive into the table (the UI's list changed)
  virtual void onBroughtBack(const ContactInfo& c) { (void)c; }

  // ---- MeshCore overrides; a subclass that overrides them calls these
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data,
                    size_t app_data_len) override;
  bool shouldAutoAddContactType(uint8_t type) const override { return !_book || keepInTable(type); }
  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data,
                      size_t len) override;
  bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len,
                      uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  // Called by the subclass's onContactOverwrite(): the contact leaves the table, it stays in the archive
  void archiveLeaving(const uint8_t* pub_key, bool keep);

private:
  bool loadCandidates();
  int tableIndexOf(const uint8_t* pubKey, const uint8_t* hash) const;   // its index among the table's matches
  bool takeEcdh();
  ContactInfo* promote(const NodeRecord& r);

  NodeBook* _book = nullptr;
  ArchiveStats _ast;
  uint8_t _hash = 0;               // sender ID of the packet being decrypted
  int _tableMatches = 0;           // candidates of the table (BaseChatMesh) for it
  bool _loaded = false;            // archive candidates looked up for this packet
  int _nc = 0;
  NodeRecord _cand[MAX_CANDIDATES];
  uint32_t _ecdhWindowMs = 0;
  int _ecdhInWindow = 0;
};
