#include "diagnostics.hpp"
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <unistd.h>
namespace {
// Replays defer serial output to the end of their measured window; a long
// replay's slow-frame lines need more than 64 KiB.
char buffer[256*1024];unsigned used=0,dropped=0;
// Three profiler phases print at most 160 bins each (under 20 KiB), plus
// audio/frame summaries and the completion marker. Never drain serial merely
// to make room: doing so would contaminate the measurements being reported.
constexpr unsigned reportReserve=32*1024;
unsigned limit=sizeof(buffer)-reportReserve;
// stdout is a tty in KOS. fwrite therefore turns every newline into a
// synchronous dcload roundtrip. POSIX writes bypass that stdio line buffering;
// the KOS console forwards each whole chunk to its selected debug device.
size_t write_console(const char *data,size_t size){
    size_t sent=0;
    while(sent<size){
        const size_t remaining=size-sent,chunk=remaining<4096?remaining:4096;
        const ssize_t n=::write(STDOUT_FILENO,data+sent,chunk);
        if(n<0 && errno==EINTR)continue;
        if(n<=0 || size_t(n)>chunk){if(n>=0)errno=EIO;break;}
        sent+=size_t(n);
    }
    return sent;
}
void report_write_error(){
    const int error=errno;
    char message[96];
    const int n=snprintf(message,sizeof(message),"DIAGNOSTICS write failed errno=%d; unsent log retained\n",error);
    // Best effort only: do not loop or recurse into logging on a broken sink.
    if(n>0)::write(STDERR_FILENO,message,size_t(n));
    errno=error;
}
}
int sor_log(const char *format,...){
    va_list args;va_start(args,format);
    int n=vsnprintf(buffer+used,limit-used,format,args);va_end(args);
    if(n<0)return n;
    if(unsigned(n)>=limit-used){dropped++;return n;}
    used+=n;return n;
}
void sor_begin_report(){limit=sizeof(buffer);}
void sor_flush_log(){
    // Preserve ordering with startup/error printf calls before bypassing stdio.
    if(fflush(stdout)!=0){report_write_error();return;}
    const size_t sent=write_console(buffer,used);
    if(sent<used){
        memmove(buffer,buffer+sent,used-sent);used-=unsigned(sent);
        // Keep the current limit: final reports may exceed the ordinary budget.
        report_write_error();return;
    }
    used=0;
    if(dropped){
        char message[64];const int n=snprintf(message,sizeof(message),"DIAGNOSTICS dropped=%u\n",dropped);
        if(write_console(message,size_t(n))==size_t(n))dropped=0;
        else report_write_error();
    }
    limit=sizeof(buffer)-reportReserve;
}
