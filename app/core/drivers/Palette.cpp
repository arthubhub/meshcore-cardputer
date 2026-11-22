#include "Palette.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

const char* const Palette::KEYS[COUNT] = {"background", "text",       "dim",        "accent",  "ok",
                                          "warning",    "bad",        "bar",        "selection", "bubble_in",
                                          "bubble_out", "mention",    "mention_me"};

namespace {
float channel(int v, int max) {
  const float c = (float)v / (float)max;
  return c <= 0.03928f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
float luminance(uint16_t c) {
  return 0.2126f * channel((c >> 11) & 0x1F, 31) + 0.7152f * channel((c >> 5) & 0x3F, 63) +
         0.0722f * channel(c & 0x1F, 31);
}

Palette make(const char* name, const uint16_t (&c)[Palette::COUNT]) {
  Palette p;
  snprintf(p.name, sizeof(p.name), "%s", name);
  memcpy(p.color, c, sizeof(p.color));
  return p;
}

bool sameName(const char* a, const char* b) {
  for (; *a && *b; a++, b++) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  }
  return *a == *b;
}

void trim(char* s) {
  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
  size_t i = 0;
  while (s[i] && isspace((unsigned char)s[i])) i++;
  if (i) memmove(s, s + i, n - i + 1);
}
}  // namespace

float Palette::contrast(uint16_t a, uint16_t b) {
  float la = luminance(a), lb = luminance(b);
  if (la < lb) {
    const float t = la;
    la = lb;
    lb = t;
  }
  return (la + 0.05f) / (lb + 0.05f);
}

int Palette::weakPairs(const char** out, int max) const {
  struct Pair { Role text, back; float need; const char* what; };
  static const Pair pairs[] = {
      {FG, BG, 4.5f, "text on background"},          {FG, SELECT_BG, 4.5f, "text on selection"},
      {FG, BUBBLE_IN, 4.5f, "text on bubble_in"},    {FG, BUBBLE_OUT, 4.5f, "text on bubble_out"},
      {FG, BAR_BG, 4.5f, "text on bar"},             {DIM, BG, 3.0f, "dim on background"},
      {ACCENT, BG, 3.0f, "accent on background"},    {ACCENT, MENTION_BG, 3.0f, "accent on mention"},
      {BG, MENTION_ME_BG, 3.0f, "background on mention_me"}, {WARN, BG, 3.0f, "warning on background"},
      {BAD, BG, 3.0f, "bad on background"},          {OK, BG, 3.0f, "ok on background"},
  };
  int n = 0;
  for (const Pair& p : pairs) {
    if (contrast(color[p.text], color[p.back]) >= p.need) continue;
    if (out && n < max) out[n] = p.what;
    n++;
  }
  return n;
}

// ---------------------------------------------------------------- ThemeCatalog

ThemeCatalog::ThemeCatalog() {
  // Roles: background, text, dim, accent, ok, warning, bad, bar, selection, bubble in, bubble out, mention, mention me
  _builtin[0] = make("Dark", {0x0000, 0xFFFF, 0x8410, 0x05FF, 0x07E0, 0xFD20, 0xF800, 0x10A2, 0x2967, 0x3186, 0x122D,
                              0x2967, 0xFD20});   // the original colours, readable outdoors
  _builtin[1] = make("Light", {0xFFFF, 0x0000, 0x6B4D, 0x0293, 0x0420, 0xB300, 0xB800, 0xD69A, 0xAE7D, 0xE71C, 0xBEFF,
                               0xBEFF, 0xB300});  // dark text on white: for direct sunlight
  _builtin[2] = make("High contrast", {0x0000, 0xFFFF, 0xC618, 0xFFE0, 0x07E0, 0xFD20, 0xF8A2, 0x0000, 0x001F, 0x2104,
                                       0x0010, 0x001F, 0xFFE0});
  _builtin[3] = make("Night", {0x0000, 0xF800, 0xB800, 0xFA28, 0xF800, 0xFB00, 0xF800, 0x1800, 0x3000, 0x2000, 0x2800,
                               0x4000, 0xF800});  // red only: keeps night vision
  _builtin[4] = make("Amber", {0x0000, 0xFD60, 0xA3A0, 0xFF00, 0xC680, 0xFD20, 0xF940, 0x1880, 0x4200, 0x2940, 0x3180,
                               0x4200, 0xFD60});  // a monochrome terminal
}

const Palette& ThemeCatalog::at(int i) const {
  if (i >= BUILTIN && i < BUILTIN + _custom) return _customs[i - BUILTIN];
  return _builtin[i >= 0 && i < BUILTIN ? i : 0];
}

int ThemeCatalog::indexOf(const char* name) const {
  for (int i = 0; i < count(); i++) {
    if (sameName(at(i).name, name)) return i;
  }
  return -1;
}

const Palette& ThemeCatalog::find(const char* name) const {
  const int i = indexOf(name ? name : "");
  return at(i < 0 ? 0 : i);
}

const char* ThemeCatalog::step(const char* name, int delta) const {
  int i = indexOf(name);
  if (i < 0) i = 0;
  const int n = count();
  return at(((i + delta) % n + n) % n).name;
}

bool ThemeCatalog::parseColor(const char* text, uint16_t& out) {
  const char* p = text;
  if (*p == '#') p++;
  if (strlen(p) != 6) return false;
  unsigned v[3];
  for (int i = 0; i < 3; i++) {
    char hex[3] = {p[i * 2], p[i * 2 + 1], 0};
    if (!isxdigit((unsigned char)hex[0]) || !isxdigit((unsigned char)hex[1])) return false;
    v[i] = (unsigned)strtoul(hex, nullptr, 16);
  }
  out = Palette::rgb565((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2]);
  return true;
}

bool ThemeCatalog::add(const char* text, const char* fallbackName, char* error, size_t errorLen, int& warnings) {
  warnings = 0;
  if (error && errorLen) error[0] = 0;
  if (_custom >= MAX_CUSTOM) {
    snprintf(error, errorLen, "only %d themes from the card", MAX_CUSTOM);
    return false;
  }
  // The overrides are collected first and applied on the base at the end: the order of the lines does not matter
  char name[Palette::NAME_LEN + 1];
  snprintf(name, sizeof(name), "%s", fallbackName ? fallbackName : "Custom");
  int base = 0;
  uint16_t override_[Palette::COUNT] = {};
  bool set[Palette::COUNT] = {};
  bool any = false;
  int lineNo = 0;
  for (const char* line = text; line && *line;) {
    const char* end = strchr(line, '\n');
    const size_t len = end ? (size_t)(end - line) : strlen(line);
    char buf[96];
    snprintf(buf, sizeof(buf), "%.*s", (int)(len < sizeof(buf) - 1 ? len : sizeof(buf) - 1), line);
    line = end ? end + 1 : nullptr;
    lineNo++;
    trim(buf);
    if (!buf[0] || buf[0] == '#') continue;   // blank or a comment ("#RRGGBB" only appears after an "=")
    char* eq = strchr(buf, '=');
    if (!eq) {
      if (!warnings && error) snprintf(error, errorLen, "line %d: no '='", lineNo);
      warnings++;
      continue;
    }
    *eq = 0;
    char* key = buf;
    char* value = eq + 1;
    trim(key);
    trim(value);
    if (sameName(key, "name")) {
      if (value[0]) snprintf(name, sizeof(name), "%s", value);
      continue;
    }
    if (sameName(key, "base")) {
      int b = -1;
      for (int i = 0; i < BUILTIN; i++) {
        if (sameName(_builtin[i].name, value)) b = i;
      }
      if (b < 0) {
        if (!warnings && error) snprintf(error, errorLen, "line %d: no built-in theme '%s'", lineNo, value);
        warnings++;
      } else {
        base = b;
      }
      continue;
    }
    int role = -1;
    for (int r = 0; r < Palette::COUNT; r++) {
      if (sameName(Palette::KEYS[r], key)) role = r;
    }
    uint16_t c;
    if (role < 0 || !parseColor(value, c)) {
      if (!warnings && error) snprintf(error, errorLen, "line %d: %s '%s'", lineNo, role < 0 ? "unknown key" : "not a #RRGGBB colour", role < 0 ? key : value);
      warnings++;
      continue;
    }
    override_[role] = c;
    set[role] = true;
    any = true;
  }
  if (!any) {
    if (error && !error[0]) snprintf(error, errorLen, "no colour in the file");
    return false;
  }
  Palette p = _builtin[base];
  snprintf(p.name, sizeof(p.name), "%s", name);
  for (int r = 0; r < Palette::COUNT; r++) {
    if (set[r]) p.color[r] = override_[r];
  }
  if (indexOf(p.name) >= 0) {              // a built-in name, or a second file with the same name
    snprintf(error, errorLen, "a theme called '%s' already exists", p.name);
    return false;
  }
  _customs[_custom++] = p;
  return true;
}
