#include <kos.h>
#include "MegaDriveEnvironment.hpp"
#include "Logger.hpp"
#include "replay.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <malloc.h>

static pvr_ptr_t texture;
static uint16_t pixels[512*256] __attribute__((aligned(32)));
static pvr_poly_hdr_t header;
static uint16_t colorLut[512];
void Controllers::poll(){
    if(replay_poll(current))return;
    PlayerControlsState *out[]={&current.player1,&current.player2};
    for(int i=0;i<2;i++) {
        *out[i]={}; auto dev=maple_enum_dev(i,0);
        if(!dev || !(dev->info.functions&MAPLE_FUNC_CONTROLLER))continue;
        auto s=static_cast<cont_state_t*>(maple_dev_status(dev)); if(!s)continue;
        auto &p=*out[i]; p.connected=true;
        p.up=s->buttons&CONT_DPAD_UP; p.down=s->buttons&CONT_DPAD_DOWN;
        p.left=s->buttons&CONT_DPAD_LEFT; p.right=s->buttons&CONT_DPAD_RIGHT;
        p.a=s->buttons&CONT_Y; p.b=s->buttons&CONT_X; p.c=s->buttons&CONT_A;
        p.start=s->buttons&CONT_START;
    }
}
MegaDriveEnvironment::MegaDriveEnvironment(VDP::Synchronization,VDP::Scaling,VDP::SpriteLimit,uint16_t)
    :port_(state_),tile_(state_),renderer_(state_,tile_,fb_){
    state_.reset(); port_.setEnvironment(this);
    for(unsigned i=0;i<512;i++){unsigned r=(i>>6)*255/7,g=((i>>3)&7)*255/7,b=(i&7)*255/7;colorLut[i]=((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
    mem_.state.read_device=readBus; mem_.state.write_device=writeBus; mem_.state.device=this;
    texture=pvr_mem_malloc(sizeof(pixels)); if(!texture)throw std::runtime_error("VRAM allocation failed");
    pvr_poly_cxt_t c; pvr_poly_cxt_txr(&c,PVR_LIST_OP_POLY,PVR_TXRFMT_RGB565|PVR_TXRFMT_NONTWIDDLED,512,256,texture,PVR_FILTER_NONE);
    pvr_poly_compile(&header,&c);
}
MegaDriveEnvironment::~MegaDriveEnvironment(){free(rom_);pvr_mem_free(texture);}
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
    const auto start=timer_us_gettime64();
    renderer_.renderFrame();
    const auto renderDone=timer_us_gettime64();
    int h=state_.activeHeight(),w=state_.activeWidth(); if(h>256)h=256;if(w>320)w=320;
    auto b=static_cast<const uint8_t*>(fb_.getRawPointer());
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        auto p=b+y*Framebuffer::PITCH+x*3;
        pixels[y*512+x]=colorLut[(p[2]<<6)|(p[1]<<3)|p[0]];
    }
    const auto convertDone=timer_us_gettime64();
    pvr_wait_ready(); pvr_txr_load(pixels,texture,sizeof(pixels));
    pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);pvr_prim(&header,sizeof(header));
    pvr_vertex_t v{};v.z=1;v.argb=0xffffffff;
    const float xs[]={0,640,0,640},ys[]={0,0,480,480};
    for(int i=0;i<4;i++){v.flags=i==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;v.x=xs[i];v.y=ys[i];v.u=(i&1)?w/512.f:0;v.v=(i&2)?h/256.f:0;pvr_prim(&v,sizeof(v));}
    pvr_list_finish();pvr_scene_finish();
    if(frames_%120==0){printf("PROFILE raster_us=%llu convert_us=%llu\n",(unsigned long long)(renderDone-start),(unsigned long long)(convertDone-renderDone));auto mi=mallinfo();printf("SOR frame=%lu mode=%04x render_us=%llu heap_used=%d vram_free=%u faults=%lu last=%06lx\n",(unsigned long)frames_,mem_.readWord(0xffff00),(unsigned long long)(timer_us_gettime64()-start),mi.uordblks,(unsigned)pvr_mem_available(),(unsigned long)mem_.state.faults,(unsigned long)last_);}
}
void MegaDriveEnvironment::waitForInterrupt(){
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
