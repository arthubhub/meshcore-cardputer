#include "Mentions.h"
#include <string.h>

// ---------------------------------------------------------------- MentionText

int MentionText::find(const char* text, MentionSpan* out, int max) {
  int n = 0;
  if (!text) return 0;
  for (const char* p = text; *p; p++) {
    if (p[0] != '@' || p[1] != '[') continue;
    const char* name = p + 2;
    const char* end = name;
    while (*end && *end != ']' && *end != '[' && end - name <= NAME_LEN) end++;
    if (*end != ']' || end == name || end - name > NAME_LEN) continue;   // not closed, empty or too long: plain text
    if (out && n < max) {
      MentionSpan& s = out[n];
      s.start = (uint16_t)(p - text);
      s.len = (uint16_t)(end + 1 - p);
      s.nameStart = (uint16_t)(name - text);
      s.nameLen = (uint16_t)(end - name);
    }
    n++;
    p = end;
  }
  return n;
}

void MentionText::fold(const char* in, char* out, size_t cap) {
  if (!cap) return;
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && o + 1 < cap) {
    uint8_t c = *p++;
    if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a');
    else if (c == 0xC3 && *p) {        // Latin-1 letters of French: à â ä ç é è ê ë î ï ô ö ù û ü ÿ and capitals
      const uint8_t d = *p++ | 0x20;    // capitals 0x80..0x9F -> small 0xA0..0xBF
      if (d >= 0xA0 && d <= 0xA5) c = 'a';
      else if (d == 0xA7) c = 'c';
      else if (d >= 0xA8 && d <= 0xAB) c = 'e';
      else if (d >= 0xAC && d <= 0xAF) c = 'i';
      else if (d == 0xB1) c = 'n';
      else if ((d >= 0xB2 && d <= 0xB6) || d == 0xB8) c = 'o';
      else if (d >= 0xB9 && d <= 0xBC) c = 'u';
      else if (d == 0xBD || d == 0xBF) c = 'y';
      else {                            // another letter: kept as its two bytes
        out[o++] = (char)0xC3;
        if (o + 1 >= cap) break;
        c = d;
      }
    }
    out[o++] = (char)c;
  }
  out[o] = 0;
}

bool MentionText::sameName(const char* a, const char* b) {
  char fa[NAME_LEN * 2 + 1], fb[NAME_LEN * 2 + 1];
  fold(a, fa, sizeof(fa));
  fold(b, fb, sizeof(fb));
  return fa[0] && strcmp(fa, fb) == 0;
}

bool MentionText::mentions(const char* text, const char* name) {
  if (!name || !*name) return false;
  MentionSpan spans[8];
  const int n = find(text, spans, 8);
  for (int i = 0; i < n && i < 8; i++) {
    char who[NAME_LEN + 1];
    memcpy(who, text + spans[i].nameStart, spans[i].nameLen);
    who[spans[i].nameLen] = 0;
    if (sameName(who, name)) return true;
  }
  return false;
}

int MentionText::toDisplay(const char* text, char* out, size_t cap, MentionSpan* spans, int maxSpans) {
  if (!cap) return 0;
  size_t o = 0;
  int ns = 0;
  MentionSpan found[8];
  const int n = find(text, found, 8);
  size_t i = 0;
  for (int k = 0; k <= n && k <= 8; k++) {
    const size_t until = (k < n && k < 8) ? found[k].start : strlen(text);
    while (i < until && o + 1 < cap) out[o++] = text[i++];
    if (k >= n || k >= 8 || o + 1 >= cap) break;
    // "@[name]" -> "@name"
    const MentionSpan& m = found[k];
    const size_t start = o;
    out[o++] = '@';
    for (uint16_t j = 0; j < m.nameLen && o + 1 < cap; j++) out[o++] = text[m.nameStart + j];
    if (spans && ns < maxSpans) {
      MentionSpan& s = spans[ns++];
      s.start = (uint16_t)start;
      s.len = (uint16_t)(o - start);
      s.nameStart = (uint16_t)(start + 1);
      s.nameLen = (uint16_t)(o - start - 1);
    }
    i = m.start + m.len;
  }
  out[o] = 0;
  return ns;
}

bool MentionText::complete(char* draft, size_t cap, size_t at, const char* name) {
  const size_t len = strlen(draft);
  if (at >= len || draft[at] != '@' || !name || !*name) return false;
  const size_t nameLen = strlen(name);
  if (at + 2 + nameLen + 2 > cap - 1) return false;   // "@[" name "] " and the NUL
  draft[at] = '@';
  draft[at + 1] = '[';
  memcpy(draft + at + 2, name, nameLen);
  draft[at + 2 + nameLen] = ']';
  draft[at + 3 + nameLen] = ' ';
  draft[at + 4 + nameLen] = 0;
  return true;
}

// ---------------------------------------------------------------- MentionPicker

namespace {
// How well a folded name matches a folded query: 2 starts with it, 1 a word starts with it, 0 no
int score(const char* name, const char* query) {
  const size_t q = strlen(query);
  if (!q || strncmp(name, query, q) == 0) return 2;
  for (const char* p = name; *p; p++) {
    if (*p == ' ' && strncmp(p + 1, query, q) == 0) return 1;
  }
  return 0;
}
}  // namespace

void MentionPicker::update(const char* draft, MentionSource& source) {
  const size_t len = strlen(draft);
  if (!_open) {
    // Opens on an "@" just typed, at the start or after a space (an e-mail address is not a mention)
    if (!len || draft[len - 1] != '@' || (len > 1 && draft[len - 2] != ' ')) return;
    _open = true;
    _at = len - 1;
    _sel = 0;
  }
  if (_at >= len || draft[_at] != '@') {  // the "@" was deleted
    close();
    return;
  }
  const char* q = draft + _at + 1;
  if (q[0] == '[' || strlen(q) > MentionText::NAME_LEN) {   // already completed, or no name is that long
    close();
    return;
  }
  char fq[MentionText::NAME_LEN * 2 + 1];
  MentionText::fold(q, fq, sizeof(fq));
  // Two passes keep the order of preference within each rank: names that start with the query, then the others
  _n = 0;
  for (int rank = 2; rank >= 1 && _n < MAX; rank--) {
    const int total = source.count();
    for (int i = 0; i < total && _n < MAX; i++) {
      char name[MentionText::NAME_LEN + 1], fn[MentionText::NAME_LEN * 2 + 1];
      if (!source.name(i, name, sizeof(name)) || !name[0]) continue;
      MentionText::fold(name, fn, sizeof(fn));
      if (score(fn, fq) != rank) continue;
      bool dup = false;
      for (int k = 0; k < _n && !dup; k++) dup = MentionText::sameName(_names[k], name);
      if (dup) continue;
      memcpy(_names[_n], name, sizeof(_names[_n]));
      _names[_n][MentionText::NAME_LEN] = 0;
      _n++;
    }
    if (!fq[0]) break;                     // nothing typed yet: every name already ranked 2
  }
  if (_sel >= _n) _sel = _n ? _n - 1 : 0;
}

void MentionPicker::move(int delta) {
  if (!_n) return;
  _sel = (_sel + delta + _n) % _n;
}

bool MentionPicker::accept(char* draft, size_t cap) {
  if (!_open || !_n) return false;
  const bool ok = MentionText::complete(draft, cap, _at, _names[_sel]);
  if (ok) close();
  return ok;
}
