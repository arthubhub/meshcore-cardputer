// The global SPIFFS object of the dual-image companion (see SPIFFS.h in this folder). Compiled in every env
// (variant/ is in the source filter) but only defines something where the env enables the adapter.
#if CPADV_SPIFFS_ON_LITTLEFS
  #include "SPIFFS.h"

fs::SPIFFSFS SPIFFS;
#endif
