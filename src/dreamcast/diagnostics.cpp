#include "diagnostics.hpp"
#include <cstdarg>
namespace {
// Replays defer serial output to the end of their measured window; a long
// replay's slow-frame lines need more than 64 KiB.
char buffer[256*1024];unsigned used=0,dropped=0;
}
int sor_log(const char *format,...){
    va_list args;va_start(args,format);
    int n=vsnprintf(buffer+used,sizeof(buffer)-used,format,args);va_end(args);
    if(n<0)return n;
    if(unsigned(n)>=sizeof(buffer)-used){dropped++;return n;}
    used+=n;return n;
}
void sor_flush_log(){
    if(used){fwrite(buffer,1,used,stdout);used=0;}
    if(dropped){printf("DIAGNOSTICS dropped=%u\n",dropped);dropped=0;}
    fflush(stdout);
}
