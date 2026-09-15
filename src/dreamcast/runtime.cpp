#include "platform.hpp"
#include "MegaDriveEnvironment.hpp"
#include "Logger.hpp"
#include "replay.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>


void Controllers::poll(const uint8_t *ram){
    if(!replay_poll(current,ram)) platform_poll_controllers(current);
}
MegaDriveEnvironment::MegaDriveEnvironment(VDP::Synchronization,VDP::Scaling,VDP::SpriteLimit,uint16_t)
    :port_(state_),tile_(state_),renderer_(state_,tile_,fb_),audio_(platform_audio_enabled()){
    state_.reset(); port_.setEnvironment(this);
    mem_.state.read_device=readBus; mem_.state.write_device=writeBus; mem_.state.device=this;
    platform_video_init();platform_audio_init(NativeAudio::sampleRate);
}
MegaDriveEnvironment::~MegaDriveEnvironment(){free(rom_);platform_audio_shutdown();platform_video_shutdown();}
void MegaDriveEnvironment::loadROM(const std::string &path){
    FILE *f=fopen(path.c_str(),"rb");
    size_t embeddedSize=0;const auto embedded=platform_embedded_rom(embeddedSize);
    if(!f && embedded && embeddedSize==524288){
        mem_.state.rom=embedded;mem_.state.rom_size=524288;audio_.setROM(mem_.state.rom,mem_.state.rom_size);
        printf("SOR native: using embedded test ROM; direct ELF boot\n");return;
    }
    if(!f)throw std::runtime_error("Missing /cd/SOR.BIN; use disc image or embedded test ELF");
    rom_=static_cast<uint8_t*>(malloc(524288)); if(!rom_){fclose(f);throw std::runtime_error("ROM allocation failed");}
    if(fread(rom_,1,524288,f)!=524288 || fgetc(f)!=EOF){fclose(f);throw std::runtime_error("ROM size mismatch");}
    fclose(f); mem_.state.rom=rom_; mem_.state.rom_size=524288;audio_.setROM(mem_.state.rom,mem_.state.rom_size);
    printf("SOR native: ROM loaded; %s audio; no enhanced art\n",audio_.enabled?"experimental":"disabled");
}
uint32_t MegaDriveEnvironment::readBus(void *ctx,uint32_t a,unsigned w){
    auto &e=*static_cast<MegaDriveEnvironment*>(ctx);
    if(w==4)return (readBus(ctx,a,2)<<16)|readBus(ctx,a+2,2);
    if(a>=0xc00000 && a<0xc00010){
        uint32_t v=(a&0xc)==4?e.port_.readControlPort():((a&0xc)==8?e.port_.readHVCounter():e.port_.readDataPort());
        return w==1?((a&1)?v&255:v>>8):v;
    }
    if(a>=0xa00000 && a<0xa02000){
        auto i=a&8191; if(i==0x1ffd&&!e.audio_.enabled)return 0; return w==1?e.audio_.ram[i]:(e.audio_.ram[i]<<8)|e.audio_.ram[(i+1)&8191];
    }
    if(a==0xa10003 || a==0xa10005){
        int n=a==0xa10005; const auto &p=n?e.pads_.current.player2:e.pads_.current.player1;
        if(!p.connected)return 0x7f;
        uint8_t v=e.th_[n]?0x7f:0x33;
        if(p.up)v&=~1; if(p.down)v&=~2;
        if(e.th_[n]) {if(p.left)v&=~4;if(p.right)v&=~8;if(p.b)v&=~16;if(p.c)v&=~32;}
        else {if(p.a)v&=~16;if(p.start)v&=~32;}
        return v;
    }
    if(a==0xa10001)return 0x80; // overseas, NTSC, pre-TMSS
    if(a>=0xa04000 && a<=0xa04003)return e.audio_.readYM(a&3);
    if(a==0xa11100)return 0;
    if(a>=0xa10000 && a<=0xa1001f)return 0;
    e.mem_.state.faults++; e.mem_.state.last_fault_address=a; return 0;
}
void MegaDriveEnvironment::writeBus(void *ctx,uint32_t a,unsigned w,uint32_t v){
    auto &e=*static_cast<MegaDriveEnvironment*>(ctx);
    if(w==4){writeBus(ctx,a,2,v>>16);writeBus(ctx,a+2,2,v&65535);return;}
    if(a>=0xc00000 && a<0xc00008){
        if(w==1)v=(v&255)*257;
        if(a&4)e.port_.writeControlPort(v);else e.port_.writeDataPort(v);return;
    }
    if(a>=0xa00000 && a<0xa02000){e.audio_.ram[a&8191]=w==1?v:v>>8;if(w==2)e.audio_.ram[(a+1)&8191]=v;return;}
    if(a==0xa10003 || a==0xa10005){e.th_[a==0xa10005]=v&64;return;}
    if(a>=0xa04000&&a<=0xa04003){e.audio_.writeYM(a&3,v);return;}
    if(a==0xc00011){e.audio_.writePSG(v);return;}
    if(a==0xa11100){e.audio_.setBusRequest(v&0x100);return;}
    if(a==0xa11200){e.audio_.setReset(!(v&0x100));return;}
    if((a>=0xa10000&&a<0xa14004)||(a>=0xa04000&&a<=0xa04003)||a==0xc00011)return;
    e.mem_.state.faults++;e.mem_.state.last_fault_address=a;
}
void MegaDriveEnvironment::present(){
    const auto start=platform_time_us();
    if(platform_render_vdp(state_,renderer_)){
        if(frames_%600==0){auto stats=platform_memory_stats();printf("PVR frame=%lu render_us=%llu heap_used=%lu vram_free=%lu\n",(unsigned long)frames_,(unsigned long long)(platform_time_us()-start),(unsigned long)stats.heap_used,(unsigned long)stats.vram_free);}
        return;
    }
    renderer_.renderFrame();
    const auto renderDone=platform_time_us();
    int h=state_.activeHeight(),w=state_.activeWidth(); if(h>256)h=256;if(w>320)w=320;
    platform_video_present(fb_,w,h);
    if(frames_%600==0){auto stats=platform_memory_stats();printf("SOR frame=%lu mode=%04x raster_us=%llu render_us=%llu heap_used=%lu vram_free=%lu faults=%lu last=%06lx\n",(unsigned long)frames_,mem_.readWord(0xffff00),(unsigned long long)(renderDone-start),(unsigned long long)(platform_time_us()-start),(unsigned long)stats.heap_used,(unsigned long)stats.vram_free,(unsigned long)mem_.state.faults,(unsigned long)last_);}

}
void MegaDriveEnvironment::waitForInterrupt(){
    // Explicit frame waits satisfy progress. Do not carry instruction budget
    // across them and inject an extra VBlank into an otherwise normal frame.
    paceCount_=0;
    platform_observe_frame(frames_,mem_.state,fb_);
    alignas(32) int16_t samples[890*2];
    const auto audioStart=platform_time_us();
    unsigned count=audio_.renderFrame(samples,frames_%600==599?platform_time_us:nullptr);const auto synthDone=platform_time_us();if(count)platform_audio_submit(samples,count);
    if(audio_.enabled&&frames_%600==599)printf("AUDIO frame=%lu synth_us=%llu stream_us=%llu ym=%llu psg=%llu dac=%llu z80_faults=%llu\n",(unsigned long)frames_,(unsigned long long)(synthDone-audioStart),(unsigned long long)(platform_time_us()-synthDone),(unsigned long long)audio_.ymWrites,(unsigned long long)audio_.psgWrites,(unsigned long long)audio_.dacWrites,(unsigned long long)audio_.z80Faults);
    if(audio_.enabled&&frames_%600==599){
        uint32_t digest=2166136261u;
        for(unsigned i=0;i<count*2;i++){uint16_t v=samples[i];digest=(digest^(v&255))*16777619u;digest=(digest^(v>>8))*16777619u;}
        printf("AUDIO_PCM frame=%lu frames=%u fnv=%08lx\n",(unsigned long)frames_,count,(unsigned long)digest);
    }
    if(audio_.enabled&&frames_%600==599)printf("AUDIO_PARTS z80=%llu fm=%llu psg=%llu\n",audio_.profile[0],audio_.profile[1],audio_.profile[2]);
    present(); pads_.poll(mem_.state.ram); frames_++; cycles_+=896040; irq_=6;
}
void MegaDriveEnvironment::pace(){
    cycles_+=28;
    // Boot/polling paths outside the hand-written frame loop still require IRQ progress.
    if(++paceCount_>=32000){paceCount_=0;if(!irq_ && cpuInterruptMask()<6)waitForInterrupt();}
}
void MegaDriveEnvironment::reportUnhandledDispatch(m_long a){
    printf("SOR UNHANDLED %06lx caller=%06lx\n",(unsigned long)a,(unsigned long)last_);
    dumpUnhandledDispatchCpuState(); throw std::runtime_error("Untranslated dispatch");
}

void MegaDriveEnvironment::debugState(){
    printf("DIAG frame=%lu mode=%04x mailbox=%02x irq=%d last=%06lx cycles=%llu faults=%lu addr=%06lx\n",(unsigned long)frames_,mem_.readWord(0xffff00),mem_.readByte(0xfffa00),irq_,(unsigned long)last_,(unsigned long long)cycles_,(unsigned long)mem_.state.faults,(unsigned long)mem_.state.last_fault_address);
    printf("P1 type=%02x pos=%04x,%04x,%04x state=%04x health=%04x held=%02x SAT=%02x%02x%02x%02x\n",mem_.readByte(0xffb800),mem_.readWord(0xffb810),mem_.readWord(0xffb814),mem_.readWord(0xffb818),mem_.readWord(0xffb830),mem_.readWord(0xffb832),mem_.readByte(0xfffc04),state_.sat_[0],state_.sat_[1],state_.sat_[2],state_.sat_[3]);
    dumpUnhandledDispatchCpuState();
}
