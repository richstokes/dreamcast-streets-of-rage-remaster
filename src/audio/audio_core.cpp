#include "audio_core.hpp"
#include "ymfm_opn.h"
#define Z80_DISABLE_DEBUG
#define Z80_DISABLE_BREAKPOINT
#define Z80_DISABLE_NESTCHECK
#define Z80_NO_FUNCTIONAL
#include "sor_z80.hpp"
#include <algorithm>
#include <array>


struct NativeAudio::Impl:ymfm::ymfm_interface {
    NativeAudio &owner;ymfm::ym2612 fm;
    std::unique_ptr<suzukiplan::Z80> cpu;
    const uint8_t *rom=nullptr;size_t romSize=0;
    uint32_t bank=0;bool reset=true,bus=false;
    uint64_t samples=0,ztime=0,timer[2]{};
    uint32_t remainder=0;
    uint16_t tone[3]{1,1,1},counter[4]{1,1,1,16},noise=0x8000;
    uint8_t volume[4]{15,15,15,15},noiseControl=0,latch=0;
    bool polarity[4]{},noiseClock=false;
    uint8_t ymAddress[2]{};unsigned psgRemainder=0;
    explicit Impl(NativeAudio &o):owner(o),fm(*this){fm.reset();resetCpu();}
    void ymfm_set_timer(uint32_t n,int32_t clocks)override{timer[n]=clocks<0?0:samples*144+clocks;}
    void clockTimers(){for(unsigned n=0;n<2;n++)if(timer[n]&&samples*144>=timer[n]){timer[n]=0;m_engine->engine_timer_expired(n);}}
    void resetCpu(){
        cpu=std::make_unique<suzukiplan::Z80>(read,write,in,out,this);
        bank=0;ztime=samples*1008/15;
    }
    static unsigned char in(void*,unsigned short){return 255;}
    static void out(void*,unsigned short,unsigned char){}
    static unsigned char read(void *p,unsigned short a){
        auto &s=*static_cast<Impl*>(p);
        if(a<0x4000)return s.owner.ram[a&8191];
        if(a<0x6000)return s.fm.read(a&3);
        if(a>=0x8000){size_t address=(s.bank<<15)+(a&0x7fff);if(address<s.romSize)return s.rom[address];s.owner.z80Faults++;}
        return 255;
    }
    static void write(void *p,unsigned short a,unsigned char v){
        auto &s=*static_cast<Impl*>(p);
        if(a<0x4000){s.owner.ram[a&8191]=v;return;}
        if(a<0x6000){s.owner.writeYM(a&3,v);return;}
        if((a&0xff00)==0x6000){s.bank=((s.bank>>1)|((v&1)<<8))&511;return;}
        if((a&0xfff9)==0x7f11){s.owner.writePSG(v);return;}
        s.owner.z80Faults++;
    }
    void runZ80(uint64_t target){
        if(reset||bus){ztime=target;return;}
        if(ztime<target)ztime+=cpu->execute(int(target-ztime));
    }
    void psgWrite(uint8_t v){
        if(v&128)latch=(v>>4)&7;
        unsigned ch=latch>>1;
        if(latch&1)volume[ch]=v&15;
        else if(ch==3){noiseControl=v&7;noise=0x8000;}
        else if(v&128)tone[ch]=(tone[ch]&0x3f0)|(v&15);
        else tone[ch]=(tone[ch]&15)|((v&63)<<4);
    }
    int psgSample(){
        // Integrate all PSG divider edges across one YM sample (1008 master clocks).
        static constexpr int amplitude[]={2800,2224,1767,1403,1115,886,704,559,444,353,280,222,177,140,111,0};
        int sum=0;unsigned remaining=1008;
        while(remaining){
            unsigned span=std::min(remaining,240-psgRemainder);int level=0;
            for(int c=0;c<4;c++)level+=(polarity[c]?1:-1)*amplitude[volume[c]];
            sum+=level*int(span);remaining-=span;psgRemainder+=span;
            if(psgRemainder==240){
                psgRemainder=0;
                bool tone2Rise=false;
                for(int c=0;c<3;c++)if(!--counter[c]){
                    counter[c]=std::max<unsigned>(1,tone[c]);polarity[c]=!polarity[c];
                    if(c==2)tone2Rise=polarity[c];
                }
                bool shiftNoise=tone2Rise;
                if((noiseControl&3)!=3){
                    shiftNoise=false;
                    if(!--counter[3]){counter[3]=16u<<(noiseControl&3);noiseClock=!noiseClock;shiftNoise=noiseClock;}
                }
                if(shiftNoise){
                    bool feedback=(noiseControl&4)?((noise^(noise>>3))&1):(noise&1);
                    noise=(noise>>1)|(unsigned(feedback)<<15);polarity[3]=noise&1;
                }
            }
        }
        return sum/1008;
    }
};
NativeAudio::NativeAudio(bool active):enabled(active),impl(active?std::make_unique<Impl>(*this):nullptr){}
NativeAudio::~NativeAudio()=default;
void NativeAudio::setROM(const uint8_t *r,size_t n){if(impl){impl->rom=r;impl->romSize=n;}}
void NativeAudio::setReset(bool b){if(impl){impl->reset=b;if(b)impl->resetCpu();}}
void NativeAudio::setBusRequest(bool b){
    if(!impl)return;
    impl->bus=b;
    // Native callers retry BUSREQ immediately; permit the DAC driver to finish
    // its short critical section rather than deadlocking on a frozen busy flag.
    if(!b&&!impl->reset)for(int i=0;i<128&&(ram[0x1ffd]&128);i++)impl->ztime+=impl->cpu->execute(16);
}
void NativeAudio::writeYM(unsigned p,uint8_t v){
    if(!impl)return;
    ymWrites++;
    if(!(p&1))impl->ymAddress[p>>1]=v;
    else if(p==1&&impl->ymAddress[0]==0x2a)dacWrites++;
    impl->fm.write(p,v);
}
uint8_t NativeAudio::readYM(unsigned p){return impl?impl->fm.read(p):0;}
void NativeAudio::writePSG(uint8_t v){if(impl){psgWrites++;impl->psgWrite(v);}}
unsigned NativeAudio::renderFrame(int16_t *out,uint64_t (*clock)()){
    std::fill_n(profile,3,0);
    if(!impl)return 0;
    impl->remainder+=896040;unsigned n=impl->remainder/1008;impl->remainder%=1008;
    for(unsigned i=0;i<n;i++){
        auto before=clock?clock():0;
        impl->samples++;impl->runZ80(impl->samples*1008/15);impl->clockTimers();
        auto afterZ80=clock?clock():0;
        ymfm::ym2612::output_data fm;impl->fm.generate(&fm);
        auto afterFM=clock?clock():0;
        int psg=impl->psgSample();
        if(clock){profile[0]+=afterZ80-before;profile[1]+=afterFM-afterZ80;profile[2]+=clock()-afterFM;}
        for(int c=0;c<2;c++)out[i*2+c]=std::clamp<int32_t>(fm.data[c]+psg,-32768,32767);
    }
    return n;
}
