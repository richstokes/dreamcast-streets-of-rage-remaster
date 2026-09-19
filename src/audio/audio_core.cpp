#include "audio_core.hpp"
#include "ymfm_opn.h"
#define Z80_DISABLE_DEBUG
#define Z80_DISABLE_BREAKPOINT
#define Z80_DISABLE_NESTCHECK
#define Z80_NO_FUNCTIONAL
#include "sor_z80.hpp"
#include "z80_hot.hpp"
#include "dac_driver.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#ifndef __DREAMCAST__
#include <cstdio>
#include <cstdlib>
#endif


struct NativeAudio::Impl:ymfm::ymfm_interface {
    NativeAudio &owner;ymfm::ym2612 fm;
    NativeDacDriver dac;bool driverKnown=false;
    std::unique_ptr<suzukiplan::Z80> cpu;
    const uint8_t *rom=nullptr;size_t romSize=0;
    uint32_t bank=0;bool reset=true,bus=false;
    uint64_t samples=0,ztime=0,zTarget=0,ymClock=0,timer[2]{};
    unsigned zFraction=0;
    struct WriteEvent {uint16_t sample;uint8_t port,value;};
    std::array<WriteEvent,2048> events{};
    std::array<ymfm::ym2612::output_data,890> block{};
    unsigned eventCount=0,eventSample=0,eventSamples=0;bool collecting=false,burst=false,driving=false;
    // 68000 writes for the next block (sample offsets), and the merge scratch.
    std::array<WriteEvent,1024> cpuEvents{};std::array<WriteEvent,512> cpuPsg{};
    std::array<WriteEvent,3072> merged{};
    unsigned cpuEventCount=0,cpuPsgCount=0,cpuOverflows=0;bool z80AddressOpen=false;
    uint64_t instructionClock=0;std::array<uint64_t,890> sampleTargets{};
    // Catch-up frame: the next block's Z80 events are collected while the
    // 68000 runs, the Z80 advancing to the 68000's time whenever it touches
    // the Z80 bus, so bus requests stall it and commands arrive on time.
    uint64_t busStall=0;   // 68000 master clocks lost to Z80 bus reads, not yet charged
    bool catchUp=false,open=false;unsigned openSamples=0;uint64_t frameZ=0;   // catch-up starts with the first sync
    unsigned nextSamples(){remainder+=896040;const unsigned n=remainder/1008;remainder%=1008;return n;}
    bool batchable()const{return owner.nativeDac && driverKnown && !(ymMode&0x80);}
    void beginBlock(unsigned n){
        eventCount=0;collecting=true;eventSample=0;eventSamples=n;blockStart=samples;frameZ=zTarget;
        for(unsigned i=0;i<n;i++){
            samples++;zTarget+=67;zFraction+=3;
            if(zFraction>=15){zFraction-=15;zTarget++;}
            sampleTargets[i]=zTarget;
        }
    }
    void sync(uint32_t clocks){
        catchUp=true;
        if(!open)return;
        const uint64_t target=std::min<uint64_t>(frameZ+clocks/15,zTarget);
        burst=true;runZ80(target);burst=false;
    }
    // Host analysis only (SOR_YM_LOG): each chip write with the index of the
    // first output sample it affects. 10-byte records: u64 sample, port, value;
    // port 4 marks PSG writes.
    uint64_t blockStart=0;bool perSample=false;std::FILE *ymLog=nullptr;
    void logWrite(unsigned port,uint8_t value,int offset=-1){
        if(!ymLog)return;
        uint64_t at=offset>=0?samples+offset:collecting?blockStart+eventSample:samples-(perSample?1:0);
        uint8_t record[10];for(int i=0;i<8;i++)record[i]=uint8_t(at>>(i*8));record[8]=uint8_t(port);record[9]=value;
        std::fwrite(record,1,10,ymLog);
    }

    uint32_t remainder=0;
    uint16_t tone[3]{1,1,1},counter[4]{1,1,1,16},noise=0x8000;
    uint8_t volume[4]{15,15,15,15},noiseControl=0,latch=0;
    bool polarity[4]{},noiseClock=false;
    uint16_t ymBusAddress=0;uint8_t ymMode=0;
    uint8_t ymAddress[2]{};unsigned psgRemainder=0;
    static constexpr int amplitude[]={2800,2224,1767,1403,1115,886,704,559,444,353,280,222,177,140,111,0};
    explicit Impl(NativeAudio &o):owner(o),fm(*this),dac(o.ram,this,read,write){
        fm.reset();resetCpu();
#ifndef __DREAMCAST__
        if(const char *path=std::getenv("SOR_YM_LOG"))ymLog=std::fopen(path,"wb");
#endif
    }
    ~Impl(){if(ymLog)std::fclose(ymLog);}
    void ymfm_set_timer(uint32_t n,int32_t clocks)override{timer[n]=clocks<0?0:ymClock+clocks;}
    void clockTimers(){for(unsigned n=0;n<2;n++)if(timer[n]&&ymClock>=timer[n]){timer[n]=0;m_engine->engine_timer_expired(n);}}
    void resetCpu(){
        cpu=std::make_unique<suzukiplan::Z80>(read,write,in,out,this);
        cpu->busReadWait=3;
        bank=0;ztime=zTarget;dac.cancel();driverKnown=false;
    }
    static unsigned char in(void*,unsigned short){return 255;}
    static void out(void*,unsigned short,unsigned char){}
    static unsigned char read(void *p,unsigned short a){
        auto &s=*static_cast<Impl*>(p);
        if(a<0x4000)return s.owner.ram[a&8191];
        if(a<0x6000)return s.fm.read(a&3);
        if(a>=0x8000){
            // A read of the 68000 bus stalls the 68000 for 70 or 77 master
            // clocks, by the Z80 clock's phase (Genesis Plus GX, memz80.c).
            const uint64_t z=s.instructionClock+(s.driving?s.dac.eventOffset:0);
            s.busStall+=(((z*15)%7+72)/7)*7;size_t address=(s.bank<<15)+(a&0x7fff);if(address<s.romSize)return s.rom[address];s.owner.z80Faults++;}
        return 255;
    }
    static void write(void *p,unsigned short a,unsigned char v){
        auto &s=*static_cast<Impl*>(p);
        if(s.burst && a>=0x4000 && a<0x6000){
            uint64_t now=s.instructionClock+(s.driving?s.dac.eventOffset:0);
            while(s.eventSample+1<s.eventSamples && s.sampleTargets[s.eventSample]<=now)s.eventSample++;
        }
        if(a<0x4000){s.owner.ram[a&8191]=v;return;}
        if(a<0x6000){
#ifndef __DREAMCAST__
            if(s.collecting)s.owner.logTimedWrite(s.owner.frameStart68k+(s.instructionClock+(s.driving?s.dac.eventOffset:0)-s.frameZ)*15,a&3,v);
#endif
            s.owner.writeYM(a&3,v);return;
        }
        if((a&0xff00)==0x6000){s.bank=((s.bank>>1)|((v&1)<<8))&511;return;}
        if((a&0xfff9)==0x7f11){s.owner.writePSG(v);return;}
        s.owner.z80Faults++;
    }
    int runSound(int clocks){
        if(!owner.nativeDac || !driverKnown)return soundZ80Execute(*cpu,owner.ram,clocks);
        int elapsed=0;
        while(elapsed<clocks){
            instructionClock=ztime+elapsed;
            if(dac.active()){
                driving=true;dac.clockBase=ztime+elapsed;elapsed+=dac.advance(clocks-elapsed);driving=false;
                if(!dac.active())cpu->reg.PC=0x2f;
            }else if(cpu->reg.PC==0xd9){
                auto &r=cpu->reg;
                dac.start((unsigned(r.pair.D)<<8)|r.pair.E,(unsigned(r.pair.B)<<8)|r.pair.C,r.IY,r.back.C,r.SP);
                owner.nativeDacStarts++;
            }else if(cpu->reg.PC>=0x2f && cpu->reg.PC<=0x3a &&
                     (owner.ram[0x1fff]<0x81 || owner.ram[0x1fff]>=0x92)){
                elapsed+=soundZ80Idle(*cpu,owner.ram,clocks-elapsed);
            }else elapsed+=cpu->execute(1); // short command/header setup only
        }
        owner.nativeDacSamples=dac.samples;
        return elapsed;
    }
    void runZ80(uint64_t target){
        if(reset||bus){ztime=target;return;}
        if(ztime<target)ztime+=runSound(int(target-ztime));
    }
    void psgWrite(uint8_t v){
        if(v&128)latch=(v>>4)&7;
        unsigned ch=latch>>1;
        if(latch&1)volume[ch]=v&15;
        else if(ch==3){noiseControl=v&7;noise=0x8000;}
        else if(v&128)tone[ch]=(tone[ch]&0x3f0)|(v&15);
        else tone[ch]=(tone[ch]&15)|((v&63)<<4);
    }
    static unsigned dividerAdvance(uint16_t &left,unsigned period,unsigned ticks){
        if(ticks<left){left-=ticks;return 0;}
        ticks-=left;
        // A sample crosses at most five divider ticks. Most muted tones have
        // period one; avoid SH-4 software division in that common case.
        unsigned edges=1;
        if(period==1){edges+=ticks;left=1;}
        else {while(ticks>=period){ticks-=period;edges++;}left=period-ticks;}
        return edges;
    }
    // Merge 68000 writes into the Z80's collected writes by sample. Each CPU
    // keeps the shared address latch from its address write to its data
    // write (the 68000 holds BUSREQ and waits out the DAC driver's busy flag),
    // so neither splits the other's pair; a held write moves to a later sample.
    unsigned mergeEvents(unsigned n){
        unsigned a=0,z=0,count=0,last=0;int owner=0; // 1: 68000, 2: Z80
        while(a<cpuEventCount || z<eventCount){
            bool takeCpu;
            if(owner==1 && a<cpuEventCount)takeCpu=true;
            else if(owner==2 && z<eventCount)takeCpu=false;
            else if(a>=cpuEventCount)takeCpu=false;
            else if(z>=eventCount)takeCpu=true;
            else takeCpu=std::min<unsigned>(cpuEvents[a].sample,n-1)<=events[z].sample;
            WriteEvent e=takeCpu?cpuEvents[a++]:events[z++];
            e.sample=uint16_t(std::max<unsigned>(last,std::min<unsigned>(e.sample,n-1)));last=e.sample;
            owner=(e.port&1)?0:(takeCpu?1:2);
            merged[count++]=e;
        }
        cpuEventCount=0;
        return count;
    }
    bool psgMuted()const{return (volume[0]&volume[1]&volume[2]&volume[3])==15;}
    // Advance a fully muted PSG by `samples` output samples at once: the same
    // divider edges and noise shifts as that many psgSample() calls, which
    // return silence. Division here happens once per span, not per sample.
    void psgSkip(unsigned samples){
        const unsigned total=psgRemainder+samples*1008,ticks=total/240;
        psgRemainder=total-ticks*240;
        const auto advance=[ticks](uint16_t &left,unsigned period){
            if(ticks<left){left=uint16_t(left-ticks);return 0u;}
            const unsigned rest=ticks-left;left=uint16_t(period-rest%period);
            return 1+rest/period;
        };
        unsigned tone2Edges=0;const bool wasTone2=polarity[2];
        for(unsigned c=0;c<3;c++){
            const unsigned edges=advance(counter[c],std::max<unsigned>(1,tone[c]));
            if(c==2)tone2Edges=edges;
            polarity[c]^=bool(edges&1);
        }
        unsigned shifts;
        if((noiseControl&3)==3)shifts=(tone2Edges+!wasTone2)/2;
        else {
            const unsigned edges=advance(counter[3],16u<<(noiseControl&3));
            shifts=(edges+!noiseClock)/2;noiseClock^=bool(edges&1);
        }
        while(shifts--){bool feedback=(noiseControl&4)?((noise^(noise>>3))&1):(noise&1);noise=(noise>>1)|(unsigned(feedback)<<15);polarity[3]=noise&1;}
    }
    int psgSample(){
        if((volume[0]&volume[1]&volume[2]&volume[3])==15){
            unsigned ticks=(psgRemainder+1008)/240;psgRemainder=(psgRemainder+1008)%240;
            unsigned tone2Edges=0;bool wasTone2=polarity[2];
            for(unsigned c=0;c<3;c++){
                unsigned edges=dividerAdvance(counter[c],std::max<unsigned>(1,tone[c]),ticks);
                if(c==2)tone2Edges=edges;
                polarity[c]^=bool(edges&1);
            }
            unsigned shifts;
            if((noiseControl&3)==3)shifts=(tone2Edges+!wasTone2)/2;
            else {
                unsigned edges=dividerAdvance(counter[3],16u<<(noiseControl&3),ticks);
                shifts=(edges+!noiseClock)/2;noiseClock^=bool(edges&1);
            }
            while(shifts--){bool feedback=(noiseControl&4)?((noise^(noise>>3))&1):(noise&1);noise=(noise>>1)|(unsigned(feedback)<<15);polarity[3]=noise&1;}
            return 0;
        }
        // Each channel's output is +amplitude while high and -amplitude while
        // low; the sample is the mean over its 1008 master clocks. Sum each
        // channel over the sample on its own (the sum is linear, so this equals
        // walking all edges in time order). Divider edges fall on 240-clock
        // ticks; muted oscillators keep running, and tone 2's rising edges
        // clock the noise register when noise follows tone 2.
        const unsigned start=psgRemainder,total=start+1008,ticks=total/240;
        psgRemainder=total-ticks*240;
        const bool tone2Clocks=(noiseControl&3)==3,noiseAudible=volume[3]!=15;
        int sum=0;unsigned rise[4],rises=0,tone2Edges=0;const bool wasTone2=polarity[2];
        for(unsigned c=0;c<3;c++){
            const unsigned period=std::max<unsigned>(1,tone[c]);
            const int amp=amplitude[volume[c]];
            // A muted tone only advances, unless its rising edges clock audible noise.
            if(!amp && !(c==2 && tone2Clocks && noiseAudible)){
                const unsigned edges=dividerAdvance(counter[c],period,ticks);
                polarity[c]^=bool(edges&1);if(c==2)tone2Edges=edges;
                continue;
            }
            bool high=polarity[c];int level=high?amp:-amp;
            unsigned k=counter[c],pos=start;
            if(!amp){
                // Muted tone 2 clocking audible noise: its rising edges only.
                for(;k<=ticks;k+=period){high=!high;tone2Edges++;if(high)rise[rises++]=k;}
                polarity[c]=high;counter[c]=uint16_t(k-ticks);
                continue;
            }
            for(;k<=ticks;k+=period){
                const unsigned at=k*240;
                sum+=level*int(at-pos);pos=at;high=!high;level=-level;
                if(c==2){tone2Edges++;if(high)rise[rises++]=k;}
            }
            sum+=level*int(total-pos);polarity[c]=high;counter[c]=uint16_t(k-ticks);
        }
        // Noise register: locals only (lambdas here were left out of line on
        // SH-4, sending every shift's state through memory).
        const bool white=noiseControl&4;
        unsigned lfsr=noise;
        if(!noiseAudible){
            // Muted noise: only the number of shifts matters.
            unsigned shifts;
            if(tone2Clocks)shifts=(tone2Edges+!wasTone2)/2;
            else {
                const unsigned edges=dividerAdvance(counter[3],16u<<(noiseControl&3),ticks);
                shifts=(edges+!noiseClock)/2;noiseClock^=bool(edges&1);
            }
            if(shifts){
                while(shifts--){const unsigned feedback=white?((lfsr^(lfsr>>3))&1):(lfsr&1);lfsr=(lfsr>>1)|(feedback<<15);}
                noise=uint16_t(lfsr);polarity[3]=lfsr&1;
            }
            return sum/1008;
        }
        const int amp=amplitude[volume[3]];
        int level=polarity[3]?amp:-amp;unsigned pos=start;
        unsigned clocks[6],shifts=0;
        if(tone2Clocks){for(unsigned i=0;i<rises;i++)clocks[shifts++]=rise[i]*240;}
        else {
            const unsigned period=16u<<(noiseControl&3);
            unsigned k=counter[3];
            for(;k<=ticks;k+=period){noiseClock=!noiseClock;if(noiseClock)clocks[shifts++]=k*240;}
            counter[3]=uint16_t(k-ticks);
        }
        for(unsigned i=0;i<shifts;i++){
            sum+=level*int(clocks[i]-pos);pos=clocks[i];
            const unsigned feedback=white?((lfsr^(lfsr>>3))&1):(lfsr&1);
            lfsr=(lfsr>>1)|(feedback<<15);level=(lfsr&1)?amp:-amp;
        }
        if(shifts){noise=uint16_t(lfsr);polarity[3]=lfsr&1;}
        sum+=level*int(total-pos);
        return sum/1008;
    }
};
NativeAudio::NativeAudio(bool active,bool native):enabled(active),nativeDac(native),impl(active?std::make_unique<Impl>(*this):nullptr){}
NativeAudio::~NativeAudio()=default;
void NativeAudio::setROM(const uint8_t *r,size_t n){if(impl){impl->rom=r;impl->romSize=n;}}
void NativeAudio::setReset(bool b){if(impl){impl->reset=b;if(b)impl->resetCpu();else impl->driverKnown=NativeDacDriver::recognizes(ram);}}
void NativeAudio::setBusRequest(bool b){
    if(!impl)return;
    impl->bus=b;
    // Native callers retry BUSREQ immediately; permit the DAC driver to finish
    // its short critical section rather than deadlocking on a frozen busy flag.
    if(!b&&!impl->reset&&!impl->open)for(int i=0;i<128&&(ram[0x1ffd]&128);i++)impl->ztime+=impl->runSound(16);
}
void NativeAudio::sync68k(uint32_t clocks){if(impl)impl->sync(clocks);}
void NativeAudio::loadZ80(const uint8_t *zram,uint32_t bank,bool resetLine,bool busRequest,const uint16_t *r,const uint8_t *m){
    if(!impl)return;
    auto &s=*impl;
    std::copy_n(zram,8192,ram);
    s.bank=(bank>>15)&511;s.reset=resetLine;s.bus=busRequest;   // bank: the 68000 base address
    s.dac.cancel();s.driverKnown=NativeDacDriver::recognizes(ram);
    auto &z=s.cpu->reg;
    const auto pair=[](suzukiplan::Z80::RegisterPair &p,uint16_t af,uint16_t bc,uint16_t de,uint16_t hl){
        p.A=af>>8;p.F=af&255;p.B=bc>>8;p.C=bc&255;p.D=de>>8;p.E=de&255;p.H=hl>>8;p.L=hl&255;};
    z.PC=r[0];z.SP=r[1];pair(z.pair,r[2],r[3],r[4],r[5]);z.IX=r[6];z.IY=r[7];pair(z.back,r[8],r[9],r[10],r[11]);
    z.I=m[0];z.R=m[1];z.IFF=(m[2]?1:0)|(m[3]?4:0)|(m[5]?0x80:0);z.interrupt=(z.interrupt&0xfc)|(m[4]&3);
}
void NativeAudio::logTimedWrite(uint64_t clock,unsigned port,uint8_t value){
#ifndef __DREAMCAST__
    static std::FILE *file=nullptr;static uint64_t lo=0,hi=0;static bool parsed=false;
    if(!parsed){parsed=true;if(const char *spec=std::getenv("SOR_YM_TIMES")){
        unsigned long a=0,b=0;char path[512]{};
        if(std::sscanf(spec,"%lu:%lu:%511s",&a,&b,path)==3){lo=uint64_t(a)*896040;hi=uint64_t(b+1)*896040;file=std::fopen(path,"w");}}}
    if(file&&clock>=lo&&clock<hi)std::fprintf(file,"%llu %u %02x\n",(unsigned long long)clock,port,value);
#else
    (void)clock;(void)port;(void)value;
#endif
}
uint32_t NativeAudio::takeBusStall(){
    if(!impl)return 0;
    const uint32_t stall=uint32_t(impl->busStall);impl->busStall=0;return stall;
}
bool NativeAudio::dacPlaying()const{return impl&&impl->dac.active();}
void NativeAudio::blockBus68k(uint32_t from,uint32_t to){
    if(!impl||!impl->open)return;
    impl->dac.blockedFrom=impl->frameZ+from/15;impl->dac.blockedUntil=impl->frameZ+(to+14)/15;
}
void NativeAudio::writeYM(unsigned p,uint8_t v){
    if(!impl)return;
    ymWrites++;impl->logWrite(p,v);
    if(!(p&1)){impl->ymAddress[p>>1]=v;impl->ymBusAddress=((p&2)?0x100:0)|v;}
    else if(p==1&&impl->ymAddress[0]==0x2a)dacWrites++;
    if(p==1 && impl->ymBusAddress==0x27)impl->ymMode=v;
    if(impl->collecting){
        if(impl->eventCount==impl->events.size())throw std::runtime_error("Native sound event bound exceeded");
        impl->events[impl->eventCount++]={uint16_t(impl->eventSample),uint8_t(p),v};
    }else{impl->z80AddressOpen=!(p&1);impl->fm.write(p,v);}
}
void NativeAudio::writeYM68k(unsigned p,uint8_t v,uint32_t clocks){
    if(!impl)return;
    auto &s=*impl;
    if(s.cpuEventCount==s.cpuEvents.size()){s.cpuOverflows++;writeYM(p,v);return;}
    ymWrites++;s.logWrite(p,v,int(std::min<uint32_t>(clocks/1008,888)));
    if(!(p&1)){s.ymAddress[p>>1]=v;s.ymBusAddress=((p&2)?0x100:0)|v;}
    else if(p==1&&s.ymAddress[0]==0x2a)dacWrites++;
    if(p==1 && s.ymBusAddress==0x27)s.ymMode=v;
    s.cpuEvents[s.cpuEventCount++]={uint16_t(std::min<uint32_t>(clocks/1008,888)),uint8_t(p),v};
}
void NativeAudio::writePSG68k(uint8_t v,uint32_t clocks){
    if(!impl)return;
    auto &s=*impl;
    if(s.cpuPsgCount==s.cpuPsg.size()){s.cpuOverflows++;writePSG(v);return;}
    psgWrites++;s.logWrite(4,v,int(std::min<uint32_t>(clocks/1008,888)));
    s.cpuPsg[s.cpuPsgCount++]={uint16_t(std::min<uint32_t>(clocks/1008,888)),0,v};
}
uint8_t NativeAudio::readYM(unsigned p){return impl?impl->fm.read(p):0;}
void NativeAudio::writePSG(uint8_t v){if(impl){psgWrites++;impl->logWrite(4,v);impl->psgWrite(v);}}
unsigned NativeAudio::renderFrame(int16_t *out,uint64_t (*clock)(),int16_t *dacStereo){
    const unsigned n=renderBlock(out,clock,dacStereo);
    // Open the next frame's block so the Z80 can follow the 68000 through it.
    if(impl && impl->catchUp && impl->batchable()){impl->openSamples=impl->nextSamples();impl->beginBlock(impl->openSamples);impl->open=true;}
    return n;
}
unsigned NativeAudio::renderBlock(int16_t *out,uint64_t (*clock)(),int16_t *dacStereo){
    std::fill_n(profile,5,0);
    if(!impl)return 0;
    impl->fm.profile_clock=clock;impl->fm.profile_clocking=impl->fm.profile_output=0;
    // A catch-up block was opened when the previous frame was rendered.
    const bool open=impl->open;impl->open=false;
    const unsigned n=open?impl->openSamples:impl->nextSamples();
    // The hash-verified SoR DAC program writes the YM bus and reads RAM/ROM;
    // its busy-bit polls see the same always-ready interface in both paths,
    // and it does not write PSG. With CSM disabled, timer expirations affect
    // status only. Advance their exact sample clocks while collecting, then collect
    // those writes first, then render constant-register spans in one hot loop.
    // Unknown drivers and timer users retain the interleaved reference path.
    if(open || impl->batchable()){
        batchFrames++;
        auto begin=clock?clock():0;
        if(!open)impl->beginBlock(n);
        // The locked driver only polls YM busy (always ready in this model).
        // No gameplay thread writes the mailbox during synthesis. Retain each
        // write's original sample boundary using its instruction start clock.
        impl->burst=true;impl->runZ80(impl->zTarget);impl->burst=false;
        for(unsigned i=0;i<n;i++){impl->ymClock+=144;impl->clockTimers();}
        impl->collecting=false;
        const unsigned total=impl->mergeEvents(n);const auto &events=impl->merged;
        auto generated=clock?clock():0;
        unsigned at=0,event=0;
        while(at<n){
            while(event<total && events[event].sample==at){
                const auto &e=events[event++];impl->fm.write(e.port,e.value);
            }
            // Address-latch writes have no waveform effect. Apply them ahead
            // of the next data write so they do not split an otherwise constant span.
            while(event<total && !(events[event].port&1)){
                const auto &e=events[event++];impl->fm.write(e.port,e.value);
            }
            unsigned end=event<total?events[event].sample:n;
            impl->fm.sor_generate_span(impl->block.data()+at,end-at);
            if(dacStereo){
                ymfm::ym2612::output_data component;impl->fm.dac_component(component);
                for(unsigned i=at;i<end;i++)for(unsigned c=0;c<2;c++)dacStereo[i*2+c]=component.data[c];
            }
            at=end;
        }

        auto mixed=clock?clock():0;
        unsigned psgEvent=0;
        const auto mix=[&](unsigned i,int psg){
            for(unsigned c=0;c<2;c++){
                int combined=std::clamp<int32_t>(impl->block[i].data[c]+psg,-32768,32767);
                int component=dacStereo?dacStereo[i*2+c]:0;
                if(combined-component < -32768 || combined-component > 32767)component=0;
                out[i*2+c]=combined-component;if(dacStereo)dacStereo[i*2+c]=component;
            }
        };
        for(unsigned i=0;i<n;){
            while(psgEvent<impl->cpuPsgCount && std::min<unsigned>(impl->cpuPsg[psgEvent].sample,n-1)<=i)impl->psgWrite(impl->cpuPsg[psgEvent++].value);
            if(impl->psgMuted()){
                // Silent until the next PSG write: advance its state in one step.
                const unsigned end=psgEvent<impl->cpuPsgCount?std::min<unsigned>(impl->cpuPsg[psgEvent].sample,n-1):n;
                impl->psgSkip(end-i);
                for(;i<end;i++)mix(i,0);
                continue;
            }
            mix(i,impl->psgSample());i++;
        }
        while(psgEvent<impl->cpuPsgCount)impl->psgWrite(impl->cpuPsg[psgEvent++].value);
        impl->cpuPsgCount=0;
        if(clock){profile[0]=generated-begin;profile[1]=mixed-generated;profile[2]=clock()-mixed;fmWorkload=impl->fm.workload();profile[3]=impl->fm.profile_clocking;profile[4]=impl->fm.profile_output;}
        return n;
    }
    interleavedFrames++;
    unsigned cpuEvent=0,psgEvent=0;
    auto applyCpu=[&](unsigned upTo){
        // Never inside the Z80's address/data pair; keep 68000 pairs together.
        while(cpuEvent<impl->cpuEventCount && !impl->z80AddressOpen && std::min<unsigned>(impl->cpuEvents[cpuEvent].sample,n-1)<=upTo){
            const auto &e=impl->cpuEvents[cpuEvent++];impl->fm.write(e.port,e.value);
            if(!(e.port&1) && cpuEvent<impl->cpuEventCount){const auto &d=impl->cpuEvents[cpuEvent++];impl->fm.write(d.port,d.value);}
        }
    };
    for(unsigned i=0;i<n;i++){
        auto before=clock?clock():0;
        // 1008 / 15 = 67 + 3/15 Z80 clocks per sample, without wide division.
        impl->samples++;impl->zTarget+=67;impl->zFraction+=3;
        if(impl->zFraction>=15){impl->zFraction-=15;impl->zTarget++;}
        impl->ymClock+=144;impl->perSample=true;impl->runZ80(impl->zTarget);impl->perSample=false;impl->clockTimers();
        applyCpu(i);
        auto afterZ80=clock?clock():0;
        ymfm::ym2612::output_data fm;impl->fm.generate(&fm);
        auto afterFM=clock?clock():0;
        while(psgEvent<impl->cpuPsgCount && std::min<unsigned>(impl->cpuPsg[psgEvent].sample,n-1)<=i)impl->psgWrite(impl->cpuPsg[psgEvent++].value);
        int psg=impl->psgSample();
        if(clock){profile[0]+=afterZ80-before;profile[1]+=afterFM-afterZ80;profile[2]+=clock()-afterFM;}
        ymfm::ym2612::output_data dac;
        if(dacStereo)impl->fm.dac_component(dac);
        for(int c=0;c<2;c++){
            int combined=std::clamp<int32_t>(fm.data[c]+psg,-32768,32767);
            int component=dacStereo?dac.data[c]:0;
            // Keep both PCM16 stems in range and their integer sum exact, even
            // when the reference mixer clips. AICA performs the final addition.
            if(combined-component < -32768 || combined-component > 32767)component=0;
            out[i*2+c]=combined-component;
            if(dacStereo)dacStereo[i*2+c]=component;
        }
    }
    impl->z80AddressOpen=false;applyCpu(n);impl->cpuEventCount=0;
    while(psgEvent<impl->cpuPsgCount)impl->psgWrite(impl->cpuPsg[psgEvent++].value);
    impl->cpuPsgCount=0;
    return n;
}
