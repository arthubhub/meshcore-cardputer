#pragma once
#include "../App.h"
#include "../Widgets.h"
#include "SettingsScreens.h"

// "Press D again to confirm" helper for destructive actions.
struct ConfirmPress {
  uint32_t until = 0;
  int target = -1;
  // First press arms (returns false), second press within 3 s on the same target confirms.
  bool check(App& app, int t, const char* what);
};

// Conversation thread: bubbles, delivery status, compose line.
class ChatScreen : public Screen {
public:
  static constexpr int WINDOW = 10;          // messages loaded at once (~2 KB)
  static constexpr int MAX_LINES = 8;

  // Prepares the screen for a conversation; then App::push(&chat).
  void open(App& app, const ConvKey& key, const char* name);
  const ConvKey& key() const { return _key; }

  const char* title() const override { return _title; }
  const char* help() const override { return "Entrée:envoi Fn+;.:défiler Fn+`:retour"; }
  bool wantsText() const override { return true; }
  void onEnter(App& app) override;
  void onResume(App& app) override { reload(app); }
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  bool onEvent(App& app, const MeshEvent& ev) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  struct Layout {
    uint8_t lines;
    uint8_t height;
    uint8_t width;
    TextLine line[MAX_LINES];
  };
  void reload(App& app);
  void layout(lgfx::LovyanGFX& g);
  void drawBubble(App& app, lgfx::LovyanGFX& g, const StoredMsg& m, const Layout& l, int y);
  void drawCompose(App& app, lgfx::LovyanGFX& g);

  ConvKey _key;
  char _title[32] = {};
  StoredMsg _msgs[WINDOW];
  Layout _lay[WINDOW];
  int _n = 0;
  int _scroll = 0;            // messages skipped from the newest
  uint32_t _total = 0;
  bool _layoutValid = false;
  char _draft[MSG_TEXT_MAX + 1] = {};
  int _maxLen = MSG_TEXT_MAX;
  bool _cursorOn = true;
  uint32_t _lastBlink = 0;
};

class ContactDetailScreen;

class ContactsScreen : public Screen {
public:
  ContactsScreen(ChatScreen& chat, ContactDetailScreen& detail) : _chat(chat), _detail(detail) {}
  // pick = true: "new message" mode, Enter opens the chat directly.
  void setPickMode(bool pick) { _pick = pick; }

  const char* title() const override { return _title; }
  const char* help() const override;
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  bool onEvent(App& app, const MeshEvent& ev) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  void syncSelection(MeshService& mesh);   // the selection follows the contact when the list re-sorts
  void remember(MeshService& mesh);

  ChatScreen& _chat;
  ContactDetailScreen& _detail;
  ListState _list;
  ConvKey _selKey;
  ConfirmPress _confirm;
  bool _pick = false;
  char _title[32] = "Contacts";
};

class CompassScreen;

class ContactDetailScreen : public Screen {
public:
  explicit ContactDetailScreen(ChatScreen& chat) : _chat(chat) {}
  void show(const ConvKey& key) { _key = key; }
  void setCompass(CompassScreen* compass) { _compass = compass; }

  const char* title() const override { return "Contact"; }
  const char* help() const override { return "M:écrire F:favori X:chemin B:boussole"; }
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  ChatScreen& _chat;
  CompassScreen* _compass = nullptr;
  ConvKey _key;
  uint32_t _lastRefresh = 0;
};

class ConversationsScreen : public Screen {
public:
  ConversationsScreen(ChatScreen& chat, ContactsScreen& contacts, TextInputScreen& input)
    : _chat(chat), _contacts(contacts), _input(input) {}

  const char* title() const override { return "Messages"; }
  const char* help() const override { return "Entrée:ouvrir N:nouv. J:canal D:suppr."; }
  void onEnter(App& app) override { _list = ListState(); _selKey = ConvKey(); }
  bool onKey(App& app, const KeyEvent& e) override;
  bool onEvent(App& app, const MeshEvent& ev) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

  static constexpr int ROW_H = 2 * theme::LINE_H;
  static constexpr int VISIBLE = theme::CONTENT_H / ROW_H;   // 4

private:
  static void onJoinDone(App& app, const char* text, void* ctx);
  void syncSelection(MessageStore& st);   // re-finds the selected conversation after re-sorting
  void remember(MessageStore& st);

  ChatScreen& _chat;
  ContactsScreen& _contacts;
  TextInputScreen& _input;
  ListState _list;
  ConvKey _selKey;
  ConfirmPress _confirm;
};
