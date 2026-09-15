#include "platform.hpp"
#include "MegaDriveEnvironment.hpp"
#include "Logger.hpp"
#include "replay.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>


void Controllers::poll(){
    if(!replay_poll(current)) platform_poll_controllers(current);
}
MegaDriveEnvironment::MegaDriveEnvironment(VDP::Synchronization,VDP::Scaling,VDP::SpriteLimit,uint16_t)
    :port_(state_),tile_(state_),renderer_(state_,tile_,fb_){
    state_.reset(); port_.setEnvironment(this);
    mem_.state.read_device=readBus; mem_.state.write_device=writeBus; mem_.state.device=this;
    platform_video_init();
}
MegaDriveEnvironment::~MegaDriveEnvironment(){free(rom_);platform_video_shutdown();}
void MegaDriveEnvironment::loadROM(const std::string &path){
    FILE *f=fopen(path.c_str(),"rb"); if(!f)throw std::runtime_error("Missing /cd/SOR.BIN; boot disc image for assets");
    rom_=static_cast<uint8_t*>(malloc(524288)); if(!rom_){fclose(f);throw std::runtime_error("ROM allocation failed");}
    if(fread(rom_,1,524288,f)!=524288 || fgetc(f)!=EOF){fclose(f);throw std::runtime_error("ROM size mismatch");}
    fclose(f); mem_.state.rom=rom_; mem_.state.rom_size=524288;
    printf("SOR native: ROM loaded; silent development build; no enhanced art\n");
}
uint32_t MegaDriveEnvironment::readBus(void *ctx,uint32_t a,unsigned w){
    auto &e=*static_cast<MegaDriveEnvironment*>(ctx);
    if(w==4)return (readBus(ctx,a,2)<<16)|readBus(ctx,a+2,2);
    if(a>=0xc00000 && a<0xc00010){
        uint32_t v=(a&0xc)==4?e.port_.readControlPort():((a&0xc)==8?e.port_.readHVCounter():e.port_.readDataPort());
        return w==1?((a&1)?v&255:v>>8):v;
    }
    if(a>=0xa00000 && a<0xa02000){
        auto i=a&8191; if(i==0x1ffd)return 0; return w==1?e.z80_.ram[i]:(e.z80_.ram[i]<<8)|e.z80_.ram[(i+1)&8191];
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
    if(a==0xa11100 || (a>=0xa04000 && a<=0xa04003))return 0; // silent checkpoint, ready
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
    if(a>=0xa00000 && a<0xa02000){e.z80_.ram[a&8191]=w==1?v:v>>8;if(w==2)e.z80_.ram[(a+1)&8191]=v;return;}
    if(a==0xa10003 || a==0xa10005){e.th_[a==0xa10005]=v&64;return;}
    if((a>=0xa10000&&a<0xa14004)||(a>=0xa04000&&a<=0xa04003)||a==0xc00011)return;
    e.mem_.state.faults++;e.mem_.state.last_fault_address=a;
}
void MegaDriveEnvironment::present(){
    const auto start=platform_time_us();
    renderer_.renderFrame();
    const auto renderDone=platform_time_us();
    int h=state_.activeHeight(),w=state_.activeWidth(); if(h>256)h=256;if(w>320)w=320;
    platform_video_present(fb_,w,h);
    if(frames_%120==0){auto stats=platform_memory_stats();printf("SOR frame=%lu mode=%04x raster_us=%llu render_us=%llu heap_used=%lu vram_free=%lu faults=%lu last=%06lx\n",(unsigned long)frames_,mem_.readWord(0xffff00),(unsigned long long)(renderDone-start),(unsigned long long)(platform_time_us()-start),(unsigned long)stats.heap_used,(unsigned long)stats.vram_free,(unsigned long)mem_.state.faults,(unsigned long)last_);}

}
void MegaDriveEnvironment::waitForInterrupt(){
    // Explicit frame waits satisfy progress. Do not carry instruction budget
    // across them and inject an extra VBlank into an otherwise normal frame.
    paceCount_=0;
    platform_observe_frame(frames_,mem_.state,fb_);
    if(frames_%120==0)debugState();
    present(); pads_.poll(); frames_++; cycles_+=896040; irq_=6;
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
