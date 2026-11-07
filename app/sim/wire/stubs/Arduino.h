#pragma once
// The little of Arduino that MeshCore's network code needs, to compile it on a PC (see ../test_wire.cpp).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Stream.h"
#include "pgmspace.h"
inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
inline char* ltoa(long v, char* s, int) { sprintf(s, "%ld", v); return s; }
