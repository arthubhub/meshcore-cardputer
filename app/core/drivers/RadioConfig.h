#pragma once
// The radio parameters of the settings (radio presets outside the EU): a preset of RadioPresets.h or the user's own
// values. Every user of the radio parameters goes through this class: the radio, the System screen, the airtime
// meter, the demo.
#include <stddef.h>
#include <stdint.h>
#include "../RadioPresets.h"

struct UiSettings;

struct RadioParams {
  float freqMHz = 869.618f;
  float bwKHz = 62.5f;
  uint8_t sf = 8;
  uint8_t cr = 5;
  bool operator==(const RadioParams& o) const;
  bool operator!=(const RadioParams& o) const { return !(*this == o); }
};

class RadioConfig {
public:
  // SX1262 limits, and the band the Cap LoRa-1262 is built for: M5Stack gives 868-923 MHz, a nominal range; the
  // official presets at 923.125 MHz (Australia SA/WA and QLD, Brazil) are kept, 927.875 MHz (Southern California) is not
  static constexpr float SX1262_MIN_MHZ = 150.0f, SX1262_MAX_MHZ = 960.0f;
  static constexpr float CAP_MIN_MHZ = 868.0f, CAP_MAX_MHZ = 923.5f;
  static const float BANDWIDTHS[];       // kHz, the SX1262's LoRa bandwidths
  static const int BANDWIDTH_COUNT;

  static RadioParams of(const UiSettings& s);
  static bool isCustom(const UiSettings& s);
  // "EU/UK Narrow", or "Custom" / "Perso"
  static const char* name(const UiSettings& s);
  // The user's own values from `p` (frequency rounded to the kHz, bandwidth to a valid one, SF 5-12, CR 5-8)
  static void setCustom(UiSettings& s, const RadioParams& p);
  // Moves to the next (+1) or previous (-1) preset of the display order; Custom comes after the last one
  static void step(UiSettings& s, int delta);
  // Chooses a preset (or RADIO_PRESET_CUSTOM). A preset that sets its network's path ID size sets it too: returns
  // true when that happened
  static bool choose(UiSettings& s, uint8_t preset);
  static bool frequencyValid(float mhz) { return mhz >= SX1262_MIN_MHZ && mhz <= SX1262_MAX_MHZ; }
  static bool inCapBand(float mhz) { return mhz >= CAP_MIN_MHZ && mhz <= CAP_MAX_MHZ; }
  // Parses "910.525" or "910,525" (MHz); false when it is not a frequency the SX1262 can use
  static bool parseFrequency(const char* text, float& mhz);
  static int bandwidthIndex(float khz);  // the nearest valid bandwidth
  // "869.618 BW62.5 SF8 CR5"
  static void describe(const RadioParams& p, char* out, size_t n);
};
