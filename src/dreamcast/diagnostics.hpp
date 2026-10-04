#pragma once
#include <cstdio>
#ifdef __DREAMCAST__
// Bounded RAM logging avoids synchronous serial writes in the frame loop.
int sor_log(const char *format,...) __attribute__((format(printf,1,2)));
// Allow final benchmark reports to use space held back from ordinary messages.
void sor_begin_report();
void sor_flush_log();
#else
#define sor_log std::printf
inline void sor_begin_report() {}
inline void sor_flush_log() {}
#endif
