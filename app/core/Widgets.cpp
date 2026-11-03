#include "Widgets.h"
#include <string.h>
#include <stdio.h>

using namespace theme;

void ListState::clamp(int count, int visible) {
  if (count <= 0) { sel = top = 0; return; }
  if (sel < 0) sel = 0;
  if (sel >= count) sel = count - 1;
  if (sel < top) top = sel;
  if (sel >= top + visible) top = sel - visible + 1;
  if (top > count - visible) top = count - visible;
  if (top < 0) top = 0;
}

void ListState::move(int delta, int count, int visible) {
  if (count <= 0) return;
  sel = (sel + delta + count) % count;
  clamp(count, visible);
}

void drawListRow(lgfx::LovyanGFX& g, int y, const char* label, const char* value,
                 bool selected, uint16_t valueColor, bool dimLabel) {
  const int right = SCREEN_W - MARGIN - 4;   // leave room for the scrollbar
  if (selected) g.fillRect(0, y, SCREEN_W - 4, LINE_H, SELECT_BG);
  g.setFont(&fontText);
  g.setTextDatum(lgfx::top_left);
  char buf[64];
  int valueW = 0;
  if (value && *value) {
    fitText(g, value, SCREEN_W / 2, buf, sizeof(buf));
    valueW = g.textWidth(buf);
    g.setTextColor(selected ? FG : valueColor);
    g.setTextDatum(lgfx::top_right);
    g.drawString(buf, right, y);
    g.setTextDatum(lgfx::top_left);
  }
  fitText(g, label, right - MARGIN - valueW - 6, buf, sizeof(buf));
  g.setTextColor(dimLabel ? DIM : FG);
  g.drawString(buf, MARGIN, y);
}

void drawScrollbar(lgfx::LovyanGFX& g, int top, int visible, int count) {
  if (count <= visible) return;
  const int x = SCREEN_W - 3, y0 = CONTENT_Y + 1, h = CONTENT_H - 2;
  g.drawFastVLine(x + 1, y0, h, BAR_BG);
  int barH = h * visible / count;
  if (barH < 6) barH = 6;
  int barY = y0 + (h - barH) * top / (count - visible);
  g.fillRect(x, barY, 3, barH, DIM);
}

size_t utf8Len(const char* s) {
  size_t n = 0;
  for (; *s; s++) if ((*s & 0xC0) != 0x80) n++;
  return n;
}

void utf8PopBack(char* s) {
  size_t n = strlen(s);
  if (!n) return;
  n--;
  while (n > 0 && (s[n] & 0xC0) == 0x80) n--;
  s[n] = 0;
}

bool utf8Append(char* s, size_t cap, const char* add) {
  size_t n = strlen(s), a = strlen(add);
  if (n + a + 1 > cap) return false;
  memcpy(s + n, add, a + 1);
  return true;
}

namespace {
struct AccentCycle {
  char base;
  const char* forms[5];   // UTF-8, lower case; upper case is derived (Latin-1: -0x20)
};
const AccentCycle CYCLES[] = {
  {'a', {"à", "â", "ä", nullptr}},
  {'e', {"é", "è", "ê", "ë", nullptr}},
  {'i', {"î", "ï", nullptr}},
  {'o', {"ô", "ö", nullptr}},
  {'u', {"ù", "û", "ü", nullptr}},
  {'c', {"ç", nullptr}},
};

// Latin-1 code point of a 2-byte UTF-8 sequence starting with 0xC3
void upper(const char* in, char out[3]) {
  out[0] = in[0];
  out[1] = (char)((unsigned char)in[1] - 0x20);
  out[2] = 0;
}
}  // namespace

bool composeAccent(char* s, size_t cap, char base) {
  bool isUpper = base >= 'A' && base <= 'Z';
  char lower = isUpper ? (char)(base + 32) : base;
  const AccentCycle* cyc = nullptr;
  for (const auto& c : CYCLES) if (c.base == lower) cyc = &c;
  if (!cyc) return false;

  int count = 0;
  while (count < 5 && cyc->forms[count]) count++;

  // Does the text end with one of the forms (same case)? Then step to the next one.
  size_t n = strlen(s);
  for (int i = 0; i < count; i++) {
    char f[3];
    if (isUpper) upper(cyc->forms[i], f); else { f[0] = cyc->forms[i][0]; f[1] = cyc->forms[i][1]; f[2] = 0; }
    if (n >= 2 && s[n - 2] == f[0] && s[n - 1] == f[1]) {
      s[n - 2] = 0;
      int next = (i + 1) % count;
      char g[3];
      if (isUpper) upper(cyc->forms[next], g); else { g[0] = cyc->forms[next][0]; g[1] = cyc->forms[next][1]; g[2] = 0; }
      return utf8Append(s, cap, g);
    }
  }
  // Otherwise append the first accented form.
  char f[3];
  if (isUpper) upper(cyc->forms[0], f); else { f[0] = cyc->forms[0][0]; f[1] = cyc->forms[0][1]; f[2] = 0; }
  return utf8Append(s, cap, f);
}

// ---------------------------------------------------------------- time formatting

void formatClock(uint32_t unix, int utcOffsetMin, char* out, size_t n) {
  if (!clockValid(unix)) { snprintf(out, n, "--:--"); return; }
  uint32_t t = unix + utcOffsetMin * 60;
  snprintf(out, n, "%02u:%02u", (unsigned)((t / 3600) % 24), (unsigned)((t / 60) % 60));
}

void formatAgo(uint32_t secs, char* out, size_t n) {
  if (secs < 60) snprintf(out, n, "%u s", (unsigned)secs);
  else if (secs < 3600) snprintf(out, n, "%u min", (unsigned)(secs / 60));
  else if (secs < 86400) snprintf(out, n, "%u h", (unsigned)(secs / 3600));
  else snprintf(out, n, "%u j", (unsigned)(secs / 86400));
}

static void civilDate(uint32_t days, int& d, int& m) {
  // days since 1970-01-01 -> day/month (Howard Hinnant's algorithm)
  int32_t z = (int32_t)days + 719468;
  int32_t era = z / 146097;
  uint32_t doe = z - era * 146097;
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
}

void formatWhen(uint32_t unix, uint32_t now, int utcOffsetMin, char* out, size_t n) {
  if (!clockValid(unix)) { snprintf(out, n, "--:--"); return; }
  uint32_t lt = unix + utcOffsetMin * 60, ln = now + utcOffsetMin * 60;
  if (!clockValid(now) || lt / 86400 == ln / 86400) { formatClock(unix, utcOffsetMin, out, n); return; }
  int d, m;
  civilDate(lt / 86400, d, m);
  snprintf(out, n, "%02d/%02d", d, m);
}

// ---------------------------------------------------------------- word wrap

static int textWidthN(lgfx::LovyanGFX& g, const char* s, int len) {
  char buf[MSG_WRAP_BUF];
  if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, s, len);
  buf[len] = 0;
  return g.textWidth(buf);
}

int wrapText(lgfx::LovyanGFX& g, const char* text, int width, TextLine* lines, int maxLines) {
  int count = 0;
  int pos = 0;
  const int total = strlen(text);
  while (pos < total && count < maxLines) {
    while (pos < total && text[pos] == ' ') pos++;   // no leading spaces on a line
    if (pos >= total) break;
    int lastSpace = -1, i = pos, fit = pos;
    while (i < total) {
      int next = i + 1;
      while (next < total && (text[next] & 0xC0) == 0x80) next++;   // whole code point
      if (text[i] == '\n') break;
      if (textWidthN(g, text + pos, next - pos) > width) break;
      if (text[i] == ' ') lastSpace = i;
      i = next;
      fit = i;
    }
    int end;
    if (fit >= total || text[fit] == '\n') end = fit;
    else if (lastSpace > pos) end = lastSpace;
    else end = fit > pos ? fit : pos + 1;   // word longer than a line: hard break
    lines[count].start = pos;
    lines[count].len = end - pos;
    count++;
    pos = end;
    if (pos < total && (text[pos] == ' ' || text[pos] == '\n')) pos++;
  }
  return count;
}
