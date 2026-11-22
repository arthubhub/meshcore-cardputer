#pragma once
// Mentions in messages (MESSAGING-13): "@[name]", the format of the MeshCore phone app. The closing bracket ends the
// name, so names may hold spaces and accents. Portable, no allocation.
//
//  - MentionText: the text side. Finds the mentions of a message, tells whether it mentions a given name (case and
//    French accents ignored, as people type them), turns "@[Alice B]" into "@Alice B" for the bubbles with the spans to
//    highlight, and replaces the "@query" being typed by "@[name] ".
//  - MentionPicker: the suggestions while typing. Fed with candidate names (a MentionSource: contacts, favourites
//    first, then the recent senders of the conversation), it keeps those that match the letters typed after "@":
//    names starting with them first, then names with a word starting with them.
#include <stddef.h>
#include <stdint.h>

struct MentionSpan {
  uint16_t start = 0, len = 0;            // the whole "@[name]" (or "@name" in a display text)
  uint16_t nameStart = 0, nameLen = 0;    // the name alone
};

class MentionText {
public:
  static constexpr int NAME_LEN = 31;     // MeshCore names: 32 bytes with the NUL

  // The mentions of `text`, in order: fills up to `max` and returns how many there are.
  static int find(const char* text, MentionSpan* out, int max);
  // `text` mentions `name` (case and accents ignored). An empty name is never mentioned.
  static bool mentions(const char* text, const char* name);
  // "@[name]" -> "@name" into `out` (cap bytes); `spans` (up to maxSpans) give the mentions in `out`. Returns how many
  // were written to `spans`. A text without mentions is copied as it is.
  static int toDisplay(const char* text, char* out, size_t cap, MentionSpan* spans, int maxSpans);
  // Replaces the end of `draft` from byte `at` (the '@' being typed) by "@[name] ". False when it does not fit `cap`.
  static bool complete(char* draft, size_t cap, size_t at, const char* name);
  // Case and French accents folded, for comparisons: "Élodie" and "elodie" are equal. Writes ASCII-folded bytes.
  static void fold(const char* in, char* out, size_t cap);
  // The two names are the same person (folded)
  static bool sameName(const char* a, const char* b);
};

class MentionSource {
public:
  virtual ~MentionSource() {}
  virtual int count() = 0;
  // The i-th candidate name, in the order of preference (favourites, recent senders, other contacts)
  virtual bool name(int i, char* out, size_t n) = 0;
};

class MentionPicker {
public:
  static constexpr int MAX = 4;           // suggestions shown above the compose line

  // The draft changed: the picker opens on an "@" being typed (at the end, after a space or at the start), follows the
  // letters after it, and closes when that "@" is gone or the text after it can no longer be a name.
  void update(const char* draft, MentionSource& source);
  void close() { _open = false; _n = 0; }
  bool open() const { return _open; }
  int count() const { return _n; }
  const char* suggestion(int i) const { return i >= 0 && i < _n ? _names[i] : ""; }
  int selected() const { return _sel; }
  void move(int delta);                   // Up / Down
  size_t at() const { return _at; }
  // Puts the selected name into the draft ("@[name] "); false when nothing is selected or it does not fit.
  bool accept(char* draft, size_t cap);
  // The draft's text after the "@" (the query)
  const char* query(const char* draft) const { return draft + _at + 1; }

private:
  bool _open = false;
  size_t _at = 0;
  int _n = 0, _sel = 0;
  char _names[MAX][MentionText::NAME_LEN + 1] = {};
};
