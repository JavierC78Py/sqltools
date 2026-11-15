#pragma once
#include <cstdint>
typedef int64_t clock64_t;
clock64_t clock64 ();   // milisegundos monotónicos (el original usaba GetSystemTimeAsFileTime)
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 1000   // el código original asume 1000 ticks/seg (ms), como en Windows
