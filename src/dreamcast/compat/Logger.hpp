#pragma once
#include <cstdio>
class Logger {public: template<class... T> static void log(const char *f,T... args) {std::printf(f,args...);std::printf("\n");}};
