#pragma once
// Dual-boot image (partitions_cpadv_dual.csv): app0 = standalone keyboard firmware,
// app1 = official MeshCore companion (phone app over Bluetooth). Both share the LittleFS data
// partition, so the radio identity (/identity) is the same in both modes.
// Switching = choosing the boot partition and restarting (nothing is copied or erased).
#include <stdint.h>

namespace cpadv {

enum class Slot : uint8_t { Standalone = 0, Companion = 1 };

// true when the flash holds both firmwares (dual partition table and a valid app in each slot).
bool dualBootImage();
// Slot of the running firmware (Standalone when the table has a single app).
Slot runningSlot();
// Logs, selects `s` as boot partition and restarts. Returns false (and logs why) if impossible.
bool rebootInto(Slot s);

}  // namespace cpadv
