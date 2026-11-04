#include "CpadvBoot.h"
#include "CpadvLog.h"
#include <Arduino.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <string.h>

namespace {

const esp_partition_t* slotPartition(cpadv::Slot s) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                  s == cpadv::Slot::Standalone ? ESP_PARTITION_SUBTYPE_APP_OTA_0
                                                               : ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                  nullptr);
}

bool slotHasApp(cpadv::Slot s, esp_app_desc_t* desc) {
  const esp_partition_t* p = slotPartition(s);
  return p && esp_ota_get_partition_description(p, desc) == ESP_OK;
}

}  // namespace

namespace cpadv {

bool dualBootImage() {
  static int cached = -1;
  if (cached < 0) {
    esp_app_desc_t a, b;
    bool s = slotHasApp(Slot::Standalone, &a);
    bool c = slotHasApp(Slot::Companion, &b);
    cached = s && c;
    if (cached) {
      // The descriptors carry our version and build date (written by tools/pio_version.py).
      CPLOG_I("BOOT", "dual-boot image: app0 standalone %s (built %s %s), app1 companion %s (built %s %s), running %s",
              a.version, a.date, a.time, b.version, b.date, b.time,
              runningSlot() == Slot::Standalone ? "standalone" : "companion");
      if (strncmp(a.version, b.version, sizeof(a.version)) != 0) {
        CPLOG_W("BOOT", "app0 and app1 come from different builds (%s / %s): rebuild both and run tools/make_dual.py",
                a.version, b.version);
      }
    } else {
      CPLOG_I("BOOT", "single firmware image (app0 %s, app1 %s)", s ? "ok" : "none", c ? "ok" : "none");
    }
  }
  return cached == 1;
}

Slot runningSlot() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return (p && p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) ? Slot::Companion : Slot::Standalone;
}

bool rebootInto(Slot s) {
  const char* name = s == Slot::Standalone ? "standalone (keyboard)" : "companion (phone app)";
  esp_app_desc_t d;
  if (!slotHasApp(s, &d)) {
    CPLOG_E("BOOT", "cannot switch to %s: firmware not installed (flash firmware-dual.bin)", name);
    return false;
  }
  esp_err_t err = esp_ota_set_boot_partition(slotPartition(s));
  if (err != ESP_OK) {
    CPLOG_E("BOOT", "cannot select the %s firmware: error %d", name, (int)err);
    return false;
  }
  CPLOG_I("BOOT", "restarting into the %s firmware", name);
  Serial.flush();
  delay(200);
  ESP.restart();
  return true;
}

}  // namespace cpadv
