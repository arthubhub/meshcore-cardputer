#include "DebugConsole.h"
#include <Arduino.h>
#include <CpadvLog.h>

bool DebugConsole::add(const char* name, const char* help, Handler fn) {
  if (_count >= MAX_CMDS) return false;
  _cmds[_count++] = {name, help, fn};
  return true;
}

void DebugConsole::loop() {
  int budget = 64;
  while (budget-- > 0 && Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r' || c == '\n') {
      if (_len) {
        _line[_len] = 0;
        _len = 0;
        run(_line);
      }
    } else if (_len < sizeof(_line) - 1) {
      _line[_len++] = (char)c;
    }
  }
}

void DebugConsole::run(char* line) {
  while (*line == ' ') line++;
  char* args = line;
  while (*args && *args != ' ') args++;
  if (*args) *args++ = 0;
  while (*args == ' ') args++;

  if (!strcmp(line, "help") || !strcmp(line, "?")) {
    cpadv::logRaw("[CPADV] console commands:");
    char buf[96];
    for (int i = 0; i < _count; i++) {
      snprintf(buf, sizeof(buf), "  %-10s %s", _cmds[i].name, _cmds[i].help);
      cpadv::logRaw(buf);
    }
    return;
  }
  for (int i = 0; i < _count; i++) {
    if (!strcmp(line, _cmds[i].name)) {
      CPLOG_I("CONS", "> %s %s", line, args);
      _cmds[i].fn(args);
      return;
    }
  }
  CPLOG_W("CONS", "unknown command '%s' (type help)", line);
}
