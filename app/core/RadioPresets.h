#pragma once
// LoRa presets offered in the settings. MeshCore nodes only hear each other with identical frequency, bandwidth and
// spreading factor (the coding rate travels in each packet's header: it may differ).
//
// Source: MeshCore's own app presets, https://api.meshcore.nz/api/v1/config (fetched 2026-11-18; see
// meshcore/docs/radio_presets.md). `python3 tools/radio_presets.py --check` lists what changed there since.
//
// The settings file stores the INDEX of the preset: entries are only ever appended, never moved or removed, so a
// saved choice keeps its meaning. Display order is RADIO_PRESET_ORDER. RADIO_PRESET_CUSTOM (255) is the user's own
// values (UiSettings::custom*), edited in Settings > Radio preset > Custom.
//
// Not listed: the 433 MHz presets and "USA - Southern California" (927.875 MHz): the Cap LoRa-1262 is built for
// 868-923 MHz (M5Stack). They remain reachable as Custom values, with a warning.
//
// Index 0 keeps MeshCore's firmware default, CR 5 (the app now suggests CR 8 for EU/UK Narrow; both talk to each
// other, CR 5 sends shorter packets).
#include <stdint.h>

struct RadioPreset {
  const char* name;      // short label shown in the settings (fits ~15 chars)
  float freqMHz;
  float bwKHz;
  uint8_t sf;
  uint8_t cr;
  uint8_t pathHashBytes; // the network's path ID size (app "network_settings"), 0 = not part of the preset
};

static const RadioPreset RADIO_PRESETS[] = {
  {"EU/UK Narrow", 869.618f, 62.5f, 8, 5, 0},        // 0: firmware default (MeshCore's build default)
  {"EU/UK Long Range", 869.525f, 250.0f, 11, 5, 0},  // 1: the app's "EU/UK (Deprecated)"
  {"EU/UK Medium", 869.525f, 250.0f, 10, 5, 0},      // 2: no longer in the app; kept for saved settings
  {"USA", 910.525f, 62.5f, 7, 5, 0},                 // 3.. added 2026-11-18 from the app's list
  {"Canada", 910.525f, 62.5f, 7, 5, 3},
  {"Australia", 915.800f, 250.0f, 10, 5, 2},
  {"Australia Narrow", 916.575f, 62.5f, 7, 7, 2},
  {"Australia Mid", 915.075f, 125.0f, 9, 5, 2},
  {"Australia SA WA", 923.125f, 62.5f, 8, 8, 2},
  {"Australia QLD", 923.125f, 62.5f, 8, 5, 2},
  {"New Zealand", 917.375f, 62.5f, 7, 5, 2},
  {"NZ Gisborne", 917.375f, 250.0f, 11, 5, 1},
  {"Brazil", 923.125f, 62.5f, 8, 8, 0},
  {"Costa Rica", 910.525f, 125.0f, 11, 5, 0},
  {"Czech Republic", 869.432f, 62.5f, 7, 5, 2},
  {"Hungary", 869.618f, 62.5f, 7, 5, 2},
  {"Netherlands", 869.618f, 62.5f, 7, 5, 0},
  {"NL Limburg", 869.618f, 62.5f, 8, 8, 2},
  {"Portugal 868", 869.618f, 62.5f, 7, 6, 0},
  {"Slovakia", 869.618f, 62.5f, 7, 5, 2},
  {"Switzerland", 869.618f, 62.5f, 8, 8, 0},
  {"Thailand", 921.000f, 62.5f, 7, 5, 0},
  {"Vietnam Narrow", 920.250f, 62.5f, 8, 5, 0},
  {"Vietnam Long Range", 920.250f, 250.0f, 11, 5, 0},
};
static constexpr int RADIO_PRESET_COUNT = sizeof(RADIO_PRESETS) / sizeof(RADIO_PRESETS[0]);
static constexpr uint8_t RADIO_PRESET_CUSTOM = 255;

// The order of the list in the settings: Europe (the default first), then the Americas, Oceania, Asia
static const uint8_t RADIO_PRESET_ORDER[] = {0, 1, 2, 14, 15, 16, 17, 18, 19, 20, 3, 4, 13, 12, 5, 6, 7, 8, 9, 10, 11, 21, 22, 23};
static_assert(sizeof(RADIO_PRESET_ORDER) == RADIO_PRESET_COUNT, "every preset once in the display order");
