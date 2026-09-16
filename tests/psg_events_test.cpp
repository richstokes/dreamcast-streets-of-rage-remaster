#include "audio_core.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
// Independent tick-by-tick oracle. Deliberately does not share the event
// scheduler, muted-counter fast path, or level cache used by the implementation.
struct TickPsg {
    unsigned tone[3]{1,1,1},counter[4]{1,1,1,16},noise=0x8000;
    unsigned volume[4]{15,15,15,15},control=0,latch=0,rem=0;
    bool polarity[4]{},noiseClock=false;
    static constexpr int amplitude[]={2800,2224,1767,1403,1115,886,704,559,444,353,280,222,177,140,111,0};
    void write(unsigned v){
        if(v&128)latch=(v>>4)&7;
        unsigned c=latch>>1;
        if(latch&1)volume[c]=v&15;
        else if(c==3){control=v&7;noise=0x8000;}
        else if(v&128)tone[c]=(tone[c]&0x3f0)|(v&15);
        else tone[c]=(tone[c]&15)|((v&63)<<4);
    }
    int sample(){
        int sum=0;unsigned left=1008;
        while(left){
            unsigned span=std::min(left,240-rem);
            for(unsigned c=0;c<4;c++)sum+=(polarity[c]?1:-1)*amplitude[volume[c]]*int(span);
            left-=span;rem+=span;
            if(rem==240){
                rem=0;bool rising=false;
                for(unsigned c=0;c<3;c++)if(!--counter[c]){
                    counter[c]=std::max(1u,tone[c]);polarity[c]=!polarity[c];
                    if(c==2)rising=polarity[c];
                }
                if((control&3)!=3){rising=false;if(!--counter[3]){counter[3]=16u<<(control&3);noiseClock=!noiseClock;rising=noiseClock;}}
                if(rising){unsigned feedback=(control&4)?((noise^(noise>>3))&1):(noise&1);noise=(noise>>1)|(feedback<<15);polarity[3]=noise&1;}
            }
        }
        return sum/1008;
    }
};
int main(){
    NativeAudio chip,silent;TickPsg oracle;uint32_t seed=0x51e67;
    auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    auto write=[&](unsigned v){chip.writePSG(v);oracle.write(v);};
    uint64_t samples=0;
    for(unsigned frame=0;frame<2048;frame++){
        for(unsigned c=0;c<3;c++){
            unsigned period=frame%2?random()%6:random()%1024;
            write(0x80|(c<<5)|(period&15));write(period>>4);
        }
        write(0xe0|(frame&7));
        for(unsigned c=0;c<4;c++)write(0x90|(c<<5)|(frame%32<16?15:random()%16));
        // Exercise continued data writes and noise resets while muted.
        if(frame%7==0)write(random()&127);
        int16_t actual[1780],base[1780];unsigned n=chip.renderFrame(actual);
        assert(silent.renderFrame(base)==n);
        for(unsigned i=0;i<n;i++){
            int psg=oracle.sample();
            for(unsigned c=0;c<2;c++)assert(actual[i*2+c]==std::clamp(int(base[i*2+c])+psg,-32768,32767));
        }
        samples+=n;
    }
    printf("PSG: %llu samples match tick oracle across mute, noise, divider and latch changes\n",(unsigned long long)samples);
}
