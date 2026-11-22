#include "RadioConfig.h"
#include "../I18n.h"
#include "../Services.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const float RadioConfig::BANDWIDTHS[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f};
const int RadioConfig::BANDWIDTH_COUNT = sizeof(BANDWIDTHS) / sizeof(BANDWIDTHS[0]);

bool RadioParams::operator==(const RadioParams& o) const {
  return fabsf(freqMHz - o.freqMHz) < 0.0005f && fabsf(bwKHz - o.bwKHz) < 0.05f && sf == o.sf && cr == o.cr;
}

bool RadioConfig::isCustom(const UiSettings& s) { return s.radioPreset == RADIO_PRESET_CUSTOM; }

RadioParams RadioConfig::of(const UiSettings& s) {
  RadioParams p;
  if (isCustom(s)) {
    p.freqMHz = s.customFreqKHz / 1000.0f;
    p.bwKHz = BANDWIDTHS[bandwidthIndex(s.customBwX100 / 100.0f)];
    p.sf = s.customSf >= 5 && s.customSf <= 12 ? s.customSf : 8;
    p.cr = s.customCr >= 5 && s.customCr <= 8 ? s.customCr : 5;
    if (!frequencyValid(p.freqMHz)) p.freqMHz = RADIO_PRESETS[0].freqMHz;   // a damaged file: back to the default
    return p;
  }
  const RadioPreset& r = RADIO_PRESETS[s.radioPreset < RADIO_PRESET_COUNT ? s.radioPreset : 0];
  p.freqMHz = r.freqMHz;
  p.bwKHz = r.bwKHz;
  p.sf = r.sf;
  p.cr = r.cr;
  return p;
}

const char* RadioConfig::name(const UiSettings& s) {
  if (isCustom(s)) return tr("Custom", "Perso");
  return RADIO_PRESETS[s.radioPreset < RADIO_PRESET_COUNT ? s.radioPreset : 0].name;
}

int RadioConfig::bandwidthIndex(float khz) {
  int best = 0;
  for (int i = 1; i < BANDWIDTH_COUNT; i++) {
    if (fabsf(BANDWIDTHS[i] - khz) < fabsf(BANDWIDTHS[best] - khz)) best = i;
  }
  return best;
}

void RadioConfig::setCustom(UiSettings& s, const RadioParams& p) {
  s.radioPreset = RADIO_PRESET_CUSTOM;
  float f = p.freqMHz;
  if (f < SX1262_MIN_MHZ) f = SX1262_MIN_MHZ;
  if (f > SX1262_MAX_MHZ) f = SX1262_MAX_MHZ;
  s.customFreqKHz = (uint32_t)lroundf(f * 1000.0f);
  s.customBwX100 = (uint16_t)lroundf(BANDWIDTHS[bandwidthIndex(p.bwKHz)] * 100.0f);
  s.customSf = p.sf < 5 ? 5 : p.sf > 12 ? 12 : p.sf;
  s.customCr = p.cr < 5 ? 5 : p.cr > 8 ? 8 : p.cr;
}

bool RadioConfig::choose(UiSettings& s, uint8_t preset) {
  if (preset == RADIO_PRESET_CUSTOM) {
    if (!isCustom(s)) setCustom(s, of(s));   // starts from the values in use
    return false;
  }
  if (preset >= RADIO_PRESET_COUNT) return false;
  s.radioPreset = preset;
  const uint8_t bytes = RADIO_PRESETS[preset].pathHashBytes;
  if (bytes && bytes != s.pathHashBytes) {
    s.pathHashBytes = bytes;
    return true;
  }
  return false;
}

void RadioConfig::step(UiSettings& s, int delta) {
  // Position in the display order; Custom is one more position at the end
  const int n = RADIO_PRESET_COUNT + 1;
  int pos = RADIO_PRESET_COUNT;   // Custom
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
    if (RADIO_PRESET_ORDER[i] == s.radioPreset) pos = i;
  }
  pos = ((pos + delta) % n + n) % n;
  choose(s, pos == RADIO_PRESET_COUNT ? RADIO_PRESET_CUSTOM : RADIO_PRESET_ORDER[pos]);
}

bool RadioConfig::parseFrequency(const char* text, float& mhz) {
  char buf[24];
  size_t n = 0;
  for (const char* p = text; *p && n + 1 < sizeof(buf); p++) {
    if (*p == ' ') continue;
    buf[n++] = *p == ',' ? '.' : *p;
  }
  buf[n] = 0;
  if (!n) return false;
  char* end = nullptr;
  const double v = strtod(buf, &end);
  if (!end || *end) return false;
  if (!frequencyValid((float)v)) return false;
  mhz = (float)v;
  return true;
}

void RadioConfig::describe(const RadioParams& p, char* out, size_t n) {
  snprintf(out, n, "%.3f BW%g SF%u CR%u", p.freqMHz, p.bwKHz, p.sf, p.cr);
}
