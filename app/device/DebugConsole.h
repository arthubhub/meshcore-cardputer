#pragma once
// Tiny serial console for hardware debugging (USB serial monitor, 115200, line ending LF or CR).
// Type "help" to list the commands. Commands are registered by main.cpp.
#include <stdint.h>

class DebugConsole {
public:
  using Handler = void (*)(const char* args);
  bool add(const char* name, const char* help, Handler fn);
  void loop();   // reads the serial input, runs a command per complete line

private:
  void run(char* line);
  struct Cmd { const char* name; const char* help; Handler fn; };
  static constexpr int MAX_CMDS = 16;
  Cmd _cmds[MAX_CMDS];
  int _count = 0;
  char _line[64];
  uint8_t _len = 0;
};
