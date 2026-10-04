// Exercise the actual bounded logger with interrupted/partial/broken sinks.
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include <unistd.h>

namespace test {
struct Result {ssize_t bytes;int error=0;};
std::deque<Result> results;
std::string output,errors;
std::vector<size_t> sizes;
int flushError=0;
}
static ssize_t diagnostic_write(int fd,const void *data,size_t size){
    if(fd==STDERR_FILENO){test::errors.append(static_cast<const char *>(data),size);return ssize_t(size);}
    assert(fd==STDOUT_FILENO);assert(size<=4096);test::sizes.push_back(size);
    test::Result result{ssize_t(size)};
    if(!test::results.empty()){result=test::results.front();test::results.pop_front();}
    if(result.bytes<0){errno=result.error;return -1;}
    assert(size_t(result.bytes)<=size);
    test::output.append(static_cast<const char *>(data),size_t(result.bytes));
    return result.bytes;
}
static int diagnostic_flush(FILE *){
    if(test::flushError){errno=test::flushError;return EOF;}
    return 0;
}
#define __DREAMCAST__
#define write diagnostic_write
#define fflush diagnostic_flush
#include "../src/dreamcast/diagnostics.cpp"
#undef fflush
#undef write
#undef __DREAMCAST__

int main(){
    // Many lines must be submitted in chunks, not one host roundtrip per line.
    std::string text;
    for(int i=0;i<1000;i++)text+="a line of diagnostics\n";
    sor_log("%s",text.c_str());sor_flush_log();
    assert(test::output==text);
    assert(test::sizes.size()==(text.size()+4095)/4096);

    test::output.clear();test::sizes.clear();
    sor_log("%s","interrupted short write\n");
    test::results={{-1,EINTR},{3,0}};
    sor_flush_log();
    assert(test::output=="interrupted short write\n");

    // A zero-progress write retains the unsent suffix for a later attempt.
    test::output.clear();
    sor_log("%s","preserve this entire message\n");
    test::results={{5,0},{0,0}};
    sor_flush_log();
    assert(test::output=="prese");
    assert(test::errors.find("unsent log retained")!=std::string::npos);
    sor_log("appended\n");sor_flush_log();
    assert(test::output=="preserve this entire message\nappended\n");

    // Pending stdio failure must not discard or reorder the buffered log.
    test::output.clear();test::flushError=EIO;
    sor_log("after stdout\n");sor_flush_log();assert(test::output.empty());
    test::flushError=0;sor_flush_log();assert(test::output=="after stdout\n");

    // Exhaust ordinary logging, then prove final reports survive both that
    // exhaustion and a write failure while still above the ordinary limit.
    test::output.clear();test::errors.clear();
    const std::string line(999,'x');
    for(int i=0;i<270;i++)sor_log("%s\n",line.c_str());
    sor_begin_report();
    const std::string report(28000,'R');
    sor_log("%s\n",report.c_str());sor_log("BENCHMARK complete\n");
    test::results={{1,0},{-1,EIO}};sor_flush_log();
    sor_log("retry retained\n");sor_flush_log();
    assert(test::output.find(report)!=std::string::npos);
    assert(test::output.find("BENCHMARK complete\nretry retained\n")!=std::string::npos);
    assert(test::output.find("DIAGNOSTICS dropped=41\n")!=std::string::npos);
    assert(!test::errors.empty());
    puts("diagnostics: chunking, partial writes, errors and reserved reports passed");
}
