#include "diagnostics.hpp"
#include "platform.hpp"
#include "MegaDriveEnvironment.hpp"
#include "Logger.hpp"
#include "replay.hpp"
#include "cheats.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef SOR_PC_HISTOGRAM
static void histogramFrame(uint32_t frame);
#endif


void Controllers::poll(const uint8_t *ram){
    // A replay's frame N is read at the Nth VBlank, as in the reference
    // harness, whose first frame (power-on to the first VBlank) has no VBlank:
    // its input is consumed here without being seen by the game.
    static bool powerOnFrame=true;
    if(powerOnFrame){powerOnFrame=false;PlayersControlState unused{};replay_poll(unused,ram);}
    if(!replay_poll(current,ram)) platform_poll_controllers(current);
    sor::cheats::poll(current,ram);
}
MegaDriveEnvironment::MegaDriveEnvironment(VDP::Synchronization,VDP::Scaling,VDP::SpriteLimit,uint16_t)
    :port_(state_),tile_(state_),renderer_(state_,tile_,fb_),audio_(platform_audio_enabled(),platform_audio_native_dac()){
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
        sor_log("SOR native: using embedded test ROM; direct ELF boot\n");return;
    }
    if(!f)throw std::runtime_error("Missing /cd/SOR.BIN; use disc image or embedded test ELF");
    rom_=static_cast<uint8_t*>(malloc(524288)); if(!rom_){fclose(f);throw std::runtime_error("ROM allocation failed");}
    if(fread(rom_,1,524288,f)!=524288 || fgetc(f)!=EOF){fclose(f);throw std::runtime_error("ROM size mismatch");}
    fclose(f); mem_.state.rom=rom_; mem_.state.rom_size=524288;audio_.setROM(mem_.state.rom,mem_.state.rom_size);
    sor_log("SOR native: ROM loaded; %s audio; no enhanced art\n",audio_.enabled?"experimental":"disabled");
}
uint32_t MegaDriveEnvironment::readBus(void *ctx,uint32_t a,unsigned w){
    auto &e=*static_cast<MegaDriveEnvironment*>(ctx);
    if(w==4)return (readBus(ctx,a,2)<<16)|readBus(ctx,a+2,2);
    if(a>=0xa00000 && a<0xa10000){e.cycles_+=7;e.syncAudio();} // Z80-bus access latency: one 68000 cycle (Genesis Plus GX)
    if(a>=0xc00000 && a<0xc00010){
        uint32_t v=(a&0xc)==4?e.port_.readControlPort():((a&0xc)==8?e.port_.readHVCounter():e.port_.readDataPort());
        if((a&0xc)==8&&getenv("SOR_HV_DEBUG")){static int n=0;if(n++<60)sor_log("HVREAD frame=%lu value=%04x last=%06lx frame_clock=%llu\n",(unsigned long)e.frames_,v,(unsigned long)e.last_,(unsigned long long)(e.cycles_-e.frameCycles_));}
        return w==1?((a&1)?v&255:v>>8):v;
    }
    if(a>=0xa00000 && a<0xa02000){
        auto i=a&8191; if(i==0x1ffd&&!e.audio_.enabled)return 0;
        return w==1?e.audio_.ram[i]:(e.audio_.ram[i]<<8)|e.audio_.ram[(i+1)&8191];
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
    if(a>=0xa04000 && a<=0xa04003){
        // Discrete YM2612: status (busy in bit 7) reads from $A04000 only.
        uint32_t v=e.audio_.readYM(a&3);
        if(a==0xa04000 && e.cycles_<e.ymBusyUntil_)v|=0x80;
        return v;
    }
    if(a==0xa11100)return 0;
    if(a>=0xa10000 && a<=0xa1001f)return 0;
    e.mem_.state.faults++; e.mem_.state.last_fault_address=a; return 0;
}
void MegaDriveEnvironment::writeBus(void *ctx,uint32_t a,unsigned w,uint32_t v){
    auto &e=*static_cast<MegaDriveEnvironment*>(ctx);
    if(w==4){e.longWrite_=true;writeBus(ctx,a,2,v>>16);writeBus(ctx,a+2,2,v&65535);e.longWrite_=false;return;}
    if(a>=0xa00000 && a<0xa10000){e.cycles_+=7;e.syncAudio();} // Z80-bus access latency: one 68000 cycle (Genesis Plus GX)
    if(a>=0xa11100 && a<0xa11202)e.syncAudio();
    if(a>=0xc00000 && a<0xc00008){
        if(w==1)v=(v&255)*257;
        if(a&4){
            const bool enabled=e.state_.regs_[1]&0x20;
            e.port_.writeControlPort(v);
            if(!enabled&&(e.state_.regs_[1]&0x20)&&e.vintPending_&&!e.longWrite_)e.irqHold_=true;
        }else e.port_.writeDataPort(v);
        return;
    }
    if(a>=0xa00000 && a<0xa02000){
#ifdef SOR_PC_HISTOGRAM
        e.audio_.logTimedWrite(e.cycles_-powerOn,0x100|(a&0x1fff),uint8_t(w==1?v:v>>8));
#endif
        e.audio_.ram[a&8191]=w==1?v:v>>8;if(w==2)e.audio_.ram[(a+1)&8191]=v;return;}
    if(a==0xa10003 || a==0xa10005){e.th_[a==0xa10005]=v&64;return;}
    // Emulated 68000 time since the frame began places each write in the next block.
    if(a>=0xa04000&&a<=0xa04003){
        // A data write keeps the YM2612 busy for 32 of its cycles (42 master
        // clocks each) from the next YM clock (Genesis Plus GX, discrete chip).
        if(a&1)e.ymBusyUntil_=((e.cycles_+41)/42+32)*42;
#ifdef SOR_PC_HISTOGRAM
        e.audio_.logTimedWrite(e.cycles_-powerOn,a&3,uint8_t(v));
#endif
        e.audio_.writeYM68k(a&3,v,uint32_t(e.cycles_-e.frameCycles_));return;
    }
    if(a==0xc00011){e.audio_.writePSG68k(v,uint32_t(e.cycles_-e.frameCycles_));return;}
    if(a==0xa11100){
#ifdef SOR_PC_HISTOGRAM
        e.audio_.logTimedWrite(e.cycles_-powerOn,0x4000,uint8_t((v>>8)&1));
#endif
        e.audio_.setBusRequest(v&0x100);return;}
    if(a==0xa11200){e.audio_.setReset(!(v&0x100));return;}
    if((a>=0xa10000&&a<0xa14004)||(a>=0xa04000&&a<=0xa04003)||a==0xc00011)return;
    e.mem_.state.faults++;e.mem_.state.last_fault_address=a;
}
void MegaDriveEnvironment::present(){
    const auto start=platform_time_us();
    const sor::TitleCaption title(state_,mem_.readWord(0xffff00));
    if(platform_render_vdp(state_,renderer_,title)){
        if(frames_%600==0){auto stats=platform_memory_stats();sor_log("PVR frame=%lu render_us=%llu heap_used=%lu vram_free=%lu\n",(unsigned long)frames_,(unsigned long long)(platform_time_us()-start),(unsigned long)stats.heap_used,(unsigned long)stats.vram_free);}
        return;
    }
    renderer_.renderFrame();
    title.draw([&](int x,int y,unsigned r,unsigned g,unsigned b){fb_.setPixel(x,y,b,g,r);});
    const auto renderDone=platform_time_us();
    int h=state_.activeHeight(),w=state_.activeWidth(); if(h>256)h=256;if(w>320)w=320;
    platform_video_present(fb_,w,h);
    if(frames_%600==0){auto stats=platform_memory_stats();sor_log("SOR frame=%lu mode=%04x raster_us=%llu render_us=%llu heap_used=%lu vram_free=%lu faults=%lu last=%06lx\n",(unsigned long)frames_,mem_.readWord(0xffff00),(unsigned long long)(renderDone-start),(unsigned long long)(platform_time_us()-start),(unsigned long)stats.heap_used,(unsigned long)stats.vram_free,(unsigned long)mem_.state.faults,(unsigned long)last_);}

}
void MegaDriveEnvironment::syncAudio(){
    audio_.sync68k(uint32_t(cycles_-frameCycles_));
    cycles_+=audio_.takeBusStall();
    audioSyncAt_=audio_.dacPlaying()?cycles_+1500*7:~uint64_t(0);
}
void MegaDriveEnvironment::waitForInterrupt(){
    settleInstruction();
#ifdef SOR_PC_HISTOGRAM
    stateSync();
#endif
    // The CPU idles until the next VBlank; a boundary already crossed (for
    // example during a DMA stall) is the one being waited for.
    if(getenv("SOR_PHASE_DEBUG")&&frames_>478&&frames_<492)sor_log("PHASE wait frame=%lu used=%llu of 896040 mode=%04x counter=%u\n",(unsigned long)frames_,(unsigned long long)(cycles_+frameClocks-nextVblank_),mem_.readWord(0xffff00),mem_.readWord(0xfffb08));
#ifdef SOR_PC_HISTOGRAM
    // Host analysis: where in each frame FIRST..LAST the game starts waiting.
    if(const char *b=getenv("SOR_WAIT_LOG")){unsigned long lo=0,hi=0;sscanf(b,"%lu:%lu",&lo,&hi);
        if(frames_>=lo&&frames_<=hi)sor_log("WAIT frame=%lu clocks=%llu mailbox=%02x\n",(unsigned long)frames_,(unsigned long long)(cycles_+frameClocks-nextVblank_),mem_.readByte(0xfffa00));}
#endif
    // An unmasked interrupt already pending (a VINT left pending while the VDP
    // or the mask held it off) is taken at once, without waiting.
    if(irqLevel()>cpuInterruptMask())return;
    // Z80 reads of the 68000 bus while the CPU idles only lengthen the idle.
    syncAudio();
    if(cycles_<nextVblank_){cycles_=nextVblank_;idleToVblank_=true;}
    frameBoundary();
}
void MegaDriveEnvironment::paceInterrupt(){
    // Emulated time crossed a VBlank while the CPU was running. In gameplay
    // that is a lag frame on the original too; log the first few.
    if(getenv("SOR_PHASE_DEBUG")&&frames_>478&&frames_<492)sor_log("PHASE crossed frame=%lu mode=%04x counter=%u last=%06lx\n",(unsigned long)frames_,mem_.readWord(0xffff00),mem_.readWord(0xfffb08),(unsigned long)last_);
    static unsigned lagFrames=0;
    if(mem_.readWord(0xffff00)==0x16 && lagFrames++<40)
        sor_log("LAG frame=%lu last=%06lx mask=%d\n",(unsigned long)frames_,(unsigned long)last_,cpuInterruptMask());
    frameBoundary();
}
void MegaDriveEnvironment::frameBoundary(){
#ifdef SOR_PC_HISTOGRAM
    histogramFrame(frames_);
#endif
    platform_observe_frame(frames_,mem_.state,fb_);
    alignas(32) int16_t samples[890*2],dacSamples[890*2];
    int16_t *dac=platform_audio_split_dac()?dacSamples:nullptr;
    const auto audioStart=platform_time_us();
    unsigned count=audio_.renderFrame(samples,platform_audio_profile()&&frames_%600==599?platform_time_us:nullptr,dac);if(idleToVblank_)audio_.takeBusStall();idleToVblank_=false;audioSyncAt_=audio_.dacPlaying()?nextVblank_-frameClocks:~uint64_t(0);const auto synthDone=platform_time_us();if(count)platform_audio_submit(samples,count,dac);
    if(audio_.enabled&&frames_%600==599)sor_log("AUDIO frame=%lu synth_us=%llu stream_us=%llu ym=%llu psg=%llu dac=%llu z80_faults=%llu\n",(unsigned long)frames_,(unsigned long long)(synthDone-audioStart),(unsigned long long)(platform_time_us()-synthDone),(unsigned long long)audio_.ymWrites,(unsigned long long)audio_.psgWrites,(unsigned long long)audio_.dacWrites,(unsigned long long)audio_.z80Faults);
    if(audio_.enabled&&frames_%600==599){
        uint32_t digest=2166136261u;
        for(unsigned i=0;i<count*2;i++){uint16_t v=int(samples[i])+(dac?dac[i]:0);digest=(digest^(v&255))*16777619u;digest=(digest^(v>>8))*16777619u;}
        sor_log("AUDIO_PCM frame=%lu frames=%u fnv=%08lx\n",(unsigned long)frames_,count,(unsigned long)digest);
    }
    if(audio_.enabled&&frames_%600==599)sor_log("DAC_NATIVE starts=%llu samples=%llu batch_frames=%llu interleaved_frames=%llu\n",audio_.nativeDacStarts,audio_.nativeDacSamples,audio_.batchFrames,audio_.interleavedFrames);
    if(audio_.enabled&&platform_audio_profile()&&frames_%600==599)sor_log("AUDIO_PARTS z80=%llu fm=%llu psg=%llu dynamic_ops=%lu ssg_ops=%lu live_ops=%lu fm_clock_us=%llu fm_output_us=%llu audible_ops=%lu\n",audio_.profile[0],audio_.profile[1],audio_.profile[2],(unsigned long)(audio_.fmWorkload&255),(unsigned long)((audio_.fmWorkload>>8)&255),(unsigned long)((audio_.fmWorkload>>16)&255),audio_.profile[3],audio_.profile[4],(unsigned long)(audio_.fmWorkload>>24));
    const auto presentStart=platform_time_us();
    present(); pads_.poll(mem_.state.ram); frames_++;
    frameCycles_=nextVblank_; vblankFlag_+=frameClocks; nextVblank_=vblankFlag_+vintDelay(); vintPending_=true;
    audio_.frameStart68k=frameCycles_-powerOn;
    platform_frame_parts(uint32_t(synthDone-audioStart),uint32_t(platform_time_us()-presentStart));
}
#ifdef SOR_PC_HISTOGRAM
namespace {
std::vector<uint64_t> histogram;unsigned histogramFirst=~0u,histogramLast=0;std::string histogramPath;bool histogramActive=false;
}
// Call timeline: master-clock time of each entry to a watched routine, until
// frame LAST (compare with genesis_reference.py --watch; tools/compare-calls.py).
static std::vector<uint8_t> watched;static std::vector<std::pair<uint32_t,uint64_t>> watchLog;
static unsigned long watchLast=0;static std::string watchPath;static bool watchActive=false;
void MegaDriveEnvironment::watchEnter(m_long a){
    static bool parsed=false;
    if(!parsed){parsed=true;if(const char *spec=getenv("SOR_WATCH")){
        std::string s(spec);auto i=s.find(':'),j=s.find(':',i+1);
        watchLast=std::stoul(s.substr(i+1,j-i-1));watchPath=s.substr(j+1);watched.assign(0x40000,0);
        if(FILE *f=fopen(s.substr(0,i).c_str(),"r")){unsigned pc;while(fscanf(f,"%x",&pc)==1)if(pc<0x80000)watched[pc>>1]=1;fclose(f);}
        watchActive=true;}}
    if(watchActive&&a<0x80000&&watched[a>>1])watchLog.emplace_back(a,cycles_-powerOn);   // since power-on
}
static void watchFrame(uint32_t frame){
    if(!watchActive||frame!=watchLast)return;
    watchActive=false;
    if(FILE *f=fopen(watchPath.c_str(),"w")){for(auto &e:watchLog)fprintf(f,"%x %llu\n",e.first,(unsigned long long)e.second);fclose(f);}
    sor_log("WATCH written %s (%zu entries)\n",watchPath.c_str(),watchLog.size());
}
void MegaDriveEnvironment::stateSync(){
    static const char *spec=getenv("SOR_STATE_SYNC");
    static bool done=false;
    if(!spec||done)return;
    const std::string s(spec);const auto colon=s.find(':');
    static std::vector<uint8_t> f;
    if(f.empty()){
        if(FILE *in=fopen(s.substr(0,colon).c_str(),"rb")){int c;while((c=fgetc(in))!=EOF)f.push_back(uint8_t(c));fclose(in);}
        if(f.size()<8||memcmp(f.data(),"SORSTAT1",8))throw std::runtime_error("SOR_STATE_SYNC: bad state file");}
    size_t at=8;
    const auto u8=[&]{return f[at++];};
    const auto u16=[&]{uint32_t v=f[at]<<8|f[at+1];at+=2;return v;};
    const auto u32=[&]{uint32_t v=uint32_t(f[at])<<24|f[at+1]<<16|f[at+2]<<8|f[at+3];at+=4;return v;};
    uint32_t r[16];for(auto &v:r)v=u32();
    const uint32_t sr=u32(),pc=u32();
    // The main loop waits in one of two routines, each spinning on its own
    // mailbox value ($10502 sets 1, $10514 sets 2). Sync where the reference
    // waited: same routine, same stack pointer and same return address.
    if((pc<0x1050C||pc>0x10512)&&(pc<0x1051E||pc>0x10524))
        throw std::runtime_error("SOR_STATE_SYNC: reference is not in a VBlank wait loop");
    const uint8_t *ram=f.data()+at;
    const auto ramLong=[&](uint32_t a){a&=0xffff;return uint32_t(ram[a])<<24|ram[a+1]<<16|ram[a+2]<<8|ram[a+3];};
    if(mem_.readWord(0xffff00)!=0x16||mem_.readByte(0xfffa00)!=ram[0xfa00])return;
    uint32_t regs[17];exchangeCpuState(regs,false);
    if(regs[15]!=r[15]||mem_.readLong(regs[15])!=ramLong(r[15]))return;
    done=true;
    std::copy_n(f.data()+at,65536,mem_.state.ram);at+=65536;
    std::copy_n(f.data()+at,65536,state_.vram_);at+=65536;state_.markAllVRAM();
    for(auto &c:state_.cram_)c=m_word(u16());
    for(auto &v:state_.vsram_)v=m_word(u16());
    std::copy_n(f.data()+at,24,state_.regs_);at+=24;
    state_.address_=m_word(u16());state_.code_=u8();
    if(u8())throw std::runtime_error("SOR_STATE_SYNC: control-port write pending");
    state_.pendingSecondWord_=false;state_.dmaFillPending_=false;state_.dmaEndCycle_=0;
    const int sat=state_.satBase();
    for(int i=0;i<VDPState::SAT_SIZE;i++)state_.sat_[i]=state_.vram_[(sat+i)&0xffff];
    const uint8_t *zram=f.data()+at;at+=8192;
    const uint32_t bank=u32();const uint8_t zstate=u8();
    uint16_t z[12];for(auto &v:z)v=uint16_t(u16());
    const uint8_t *misc=f.data()+at;at+=6;
    if(at!=f.size())throw std::runtime_error("SOR_STATE_SYNC: state file size");
    audio_.loadZ80(zram,bank,!(zstate&1),(zstate&2)!=0,z,misc);
    for(int i=0;i<8;i++)regs[i]=r[i];
    for(int i=0;i<7;i++)regs[8+i]=r[8+i];
    regs[15]=r[15];regs[16]=sr;
    exchangeCpuState(regs,true);
    vintPending_=false;irqHold_=false;
    if(!replay_load(s.substr(colon+1).c_str()))throw std::runtime_error("SOR_STATE_SYNC: replay");
    sor_log("STATE_SYNC frame=%lu\n",(unsigned long)frames_);
}
static void histogramParse(){
    static bool parsed=false;
    if(parsed)return;
    parsed=true;
    if(const char *spec=getenv("SOR_PC_HISTOGRAM_FRAMES")){
        std::string s(spec);auto a=s.find(':'),b=s.find(':',a+1);
        histogramFirst=std::stoul(s.substr(0,a));histogramLast=std::stoul(s.substr(a+1,b-a-1));histogramPath=s.substr(b+1);
        histogram.assign(0x800000,0);histogramActive=histogramFirst==0;}
}
void MegaDriveEnvironment::pcHistogram(unsigned pc,unsigned cpuCycles){
    histogramParse();
    if(histogramActive)histogram[(pc&0xFFFFFE)>>1]+=uint64_t(cpuCycles)*7;
}
static void histogramFrame(uint32_t frame){
    watchFrame(frame);
    histogramParse();
    if(frame+1==histogramFirst)histogramActive=true;
    if(histogramActive&&frame==histogramLast){
        histogramActive=false;
        // Same layout as the Genesis Plus GX profile: 0x40000 ROM words (master clocks).
        FILE *f=fopen(histogramPath.c_str(),"wb");
        if(f){fwrite(histogram.data(),8,0x40000,f);fclose(f);}
        uint64_t stall=histogram[0xFFFFFE>>1];
        sor_log("PC_HISTOGRAM written %s (DMA stall %llu cycles)\n",histogramPath.c_str(),(unsigned long long)(stall/7));
    }
}
#endif
void MegaDriveEnvironment::reportUnhandledDispatch(m_long a){
    sor_log("SOR UNHANDLED %06lx caller=%06lx\n",(unsigned long)a,(unsigned long)last_);
    dumpUnhandledDispatchCpuState(); throw std::runtime_error("Untranslated dispatch");
}

void MegaDriveEnvironment::debugState(){
    sor_log("DIAG frame=%lu mode=%04x mailbox=%02x irq=%d last=%06lx cycles=%llu faults=%lu addr=%06lx\n",(unsigned long)frames_,mem_.readWord(0xffff00),mem_.readByte(0xfffa00),irqLevel(),(unsigned long)last_,(unsigned long long)cycles_,(unsigned long)mem_.state.faults,(unsigned long)mem_.state.last_fault_address);
    sor_log("P1 type=%02x pos=%04x,%04x,%04x state=%04x health=%04x held=%02x SAT=%02x%02x%02x%02x\n",mem_.readByte(0xffb800),mem_.readWord(0xffb810),mem_.readWord(0xffb814),mem_.readWord(0xffb818),mem_.readWord(0xffb830),mem_.readWord(0xffb832),mem_.readByte(0xfffc04),state_.sat_[0],state_.sat_[1],state_.sat_[2],state_.sat_[3]);
    dumpUnhandledDispatchCpuState();
}
