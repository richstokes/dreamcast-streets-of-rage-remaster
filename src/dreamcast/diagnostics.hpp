#pragma once
#include <cstdio>
#ifdef __DREAMCAST__
// Bounded RAM logging avoids synchronous serial writes in the frame loop.
int sor_log(const char *format,...) __attribute__((format(printf,1,2)));
void sor_flush_log();
#else
#define sor_log std::printf
inline void sor_flush_log() {}
#endif
