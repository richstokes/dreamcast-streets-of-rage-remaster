// Opt-in statistical profiler (SOR_PC_PROFILE=1): TMU1 underflows at 10 kHz
// and samples the interrupted program counter into 32-byte bins, split between
// gameplay and everything else. tools/pc-profile.py resolves the bins. KOS
// installs only a default handler on TMU1; Flycast does not raise the
// watchdog's interval interrupt, so that timer cannot be used here.
#include "diagnostics.hpp"
#include "pc_profile.hpp"
#include "sor_audio_config.hpp"
#include <kos.h>
#include <algorithm>
namespace {
constexpr unsigned binCount=8192;
// Buckets: other, gameplay, and any frame's time beyond 17 ms (the overrun).
uint32_t keys[binCount],counts[3][binCount],totals[3],dropped;
volatile unsigned phase,ticks;
void sample(irq_t,irq_context_t *context,void*){
    timer_clear(TMU1);
    const uint32_t pc=context->pc&~31u;
    const unsigned bucket=++ticks>170?2:phase;
    const unsigned home=((pc>>5)*2654435761u)>>19;
    for(unsigned probe=0;probe<32;probe++){
        const unsigned i=(home+probe)&(binCount-1);
        if(keys[i]!=pc && keys[i])continue;
        keys[i]=pc;counts[bucket][i]++;totals[bucket]++;return;
    }
    dropped++;
}
}
void pc_profile_start(){
    if(!SOR_ENABLE_PC_PROFILE)return;
    irq_set_handler(EXC_TMU1_TUNI1,sample,nullptr);
    timer_prime(TMU1,10000,1);timer_clear(TMU1);timer_start(TMU1);
    sor_log("PCPROF sampling every 100 us\n");
}
void pc_profile_phase(bool gameplay){phase=gameplay;ticks=0;}
void pc_profile_report(){
    if(!SOR_ENABLE_PC_PROFILE)return;
    timer_stop(TMU1);
    for(unsigned p=0;p<3;p++){
        static unsigned order[binCount];
        unsigned n=0;for(unsigned i=0;i<binCount;i++)if(counts[p][i])order[n++]=i;
        std::sort(order,order+n,[&](unsigned a,unsigned b){return counts[p][a]>counts[p][b];});
        sor_log("PCPROF phase=%s samples=%u dropped=%u\n",p==2?"overrun":p?"gameplay":"other",totals[p],dropped);
        for(unsigned i=0;i<std::min(n,160u);i++)sor_log("PCPROF %u %08lx %u\n",p,(unsigned long)keys[order[i]],counts[p][order[i]]);
    }
}
