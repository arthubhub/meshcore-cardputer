#pragma once
// LoRa presets offered in the settings. MeshCore nodes only hear each other with identical
// frequency / bandwidth / spreading factor. The first entry is the firmware default.
#include <stdint.h>

struct RadioPreset {
  const char* name;     // short label shown in the settings list (fits ~14 chars)
  float freqMHz;
  float bwKHz;
  uint8_t sf;
  uint8_t cr;
};

static const RadioPreset RADIO_PRESETS[] = {
  {"EU Narrow", 869.618f, 62.5f, 8, 5},     // MeshCore EU/UK default (build default)
  {"EU Long Range", 869.525f, 250.0f, 11, 5},
  {"EU Medium", 869.525f, 250.0f, 10, 5},
};
static constexpr int RADIO_PRESET_COUNT = sizeof(RADIO_PRESETS) / sizeof(RADIO_PRESETS[0]);
