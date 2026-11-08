#pragma once
// Transmit time over the last hour (RADIO-4). European 868 MHz sub-bands limit how much of the time a device may
// transmit; MeshCore's own budget is far looser than those limits, so this meter tells the user when they get close.
// It measures and warns, it never blocks a message.
//
// Fed with MeshCore's cumulative counter (Dispatcher::getTotalAirTime). One bucket per minute: 120 bytes of RAM.
#include <stdint.h>

class AirtimeMeter {
public:
  static constexpr int BUCKETS = 60;
  static constexpr uint32_t BUCKET_MS = 60000;
  static constexpr uint32_t WINDOW_MS = BUCKETS * BUCKET_MS;
  static constexpr float WARN_SHARE = 0.8f;     // warn at 80 % of the limit...
  static constexpr float REARM_SHARE = 0.7f;    // ...and again only after falling below 70 % of it

  enum class Alert : uint8_t { None, Near, Over };

  // Regulatory duty cycle of a frequency in percent, 0 when this table has no limit for it (outside Europe's 863-870
  // MHz band, where rules differ). Indicative figures from ERC 70-03 Annex 1: check the rules of your country.
  static float limitPercent(float freqMHz);

  // Call regularly with the cumulative transmit time in ms.
  void update(uint32_t totalTxMs, uint32_t nowMs);
  uint32_t lastHourMs() const;
  float percent() const { return lastHourMs() * 100.0f / WINDOW_MS; }
  // Reports each rise of the alert level once. limitPct 0: never alerts.
  Alert pollAlert(float limitPct);

private:
  void advance(uint32_t nowMs);
  uint16_t _b[BUCKETS] = {};     // transmit ms per minute (at most 60000)
  int _cur = 0;
  uint32_t _bucketStart = 0;
  uint32_t _lastTotal = 0;
  bool _started = false;
  Alert _level = Alert::None;
};
