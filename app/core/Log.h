#pragma once
// Logging from the portable code. Implemented by variant/CpadvLog.cpp on the
// Cardputer ("[CPADV] t L TAG msg" on the USB serial) and by the simulator (stdout).
void coreLogf(char level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

#define CORE_LOGE(tag, ...) coreLogf('E', tag, __VA_ARGS__)
#define CORE_LOGW(tag, ...) coreLogf('W', tag, __VA_ARGS__)
#define CORE_LOGI(tag, ...) coreLogf('I', tag, __VA_ARGS__)
