// Log sink of the portable code (core/Log.h) for the simulator and the unit tests.
// Same format as the Cardputer's serial log. Silenced with CORE_LOG=0 in the environment,
// and captured by the unit tests (g_lastCoreLog).
#include "../core/Log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

std::string g_coreLogCapture;   // every line, for assertions in tests
bool g_coreLogQuiet = false;

void coreLogf(char level, const char* tag, const char* fmt, ...) {
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  char line[300];
  snprintf(line, sizeof(line), "[CPADV] %c %-6s %s\n", level, tag, msg);
  g_coreLogCapture += line;
  static int env = -1;
  if (env < 0) { const char* e = getenv("CORE_LOG"); env = (e && *e == '0') ? 0 : 1; }
  if (env && !g_coreLogQuiet) fputs(line, stdout);
}
