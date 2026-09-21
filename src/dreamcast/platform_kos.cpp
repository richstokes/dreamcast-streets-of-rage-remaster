#include "diagnostics.hpp"
#include <kos.h>
#include <malloc.h>
#include <stdexcept>
#include "platform.hpp"
#include "replay.hpp"
#include "pc_profile.hpp"
#include "sor_audio_config.hpp"
#include "cheats.hpp"
void dc_renderer_init();
void dc_renderer_shutdown();
bool dc_render_vdp(VDPState &,VDPRenderer &,const sor::TitleCaption &);
void dc_renderer_game_state(unsigned round,unsigned characters);
void platform_game_state(unsigned round,unsigned characters){dc_renderer_game_state(round,characters);}
static bool useGpu=true,previousToggle=false;
bool platform_render_vdp(VDPState &s,VDPRenderer &r,const sor::TitleCaption &title){return useGpu && dc_render_vdp(s,r,title);}
static pvr_ptr_t texture;
static uint16_t pixels[512*256] __attribute__((aligned(32)));
static pvr_poly_hdr_t header;
static uint16_t colorLut[512];
void platform_poll_controllers(PlayersControlState &current){
    PlayerControlsState *out[]={&current.player1,&current.player2};
    for(int i=0;i<2;i++) {
        *out[i]={}; auto dev=maple_enum_dev(i,0);
        if(!dev || !(dev->info.functions&MAPLE_FUNC_CONTROLLER))continue;
        auto s=static_cast<cont_state_t*>(maple_dev_status(dev)); if(!s)continue;
        if(SOR_SOFTWARE_TOGGLE && i==0){bool toggle=s->buttons&CONT_B;if(toggle&&!previousToggle&&!sor::cheats::menu.visible()){useGpu=!useGpu;sor_log("Renderer: %s\n",useGpu?"PowerVR":"software comparison");}previousToggle=toggle;}
        auto &p=*out[i]; p.connected=true;
        p.up=s->buttons&CONT_DPAD_UP; p.down=s->buttons&CONT_DPAD_DOWN;
        p.left=s->buttons&CONT_DPAD_LEFT; p.right=s->buttons&CONT_DPAD_RIGHT;
        p.a=s->buttons&CONT_Y; p.b=s->buttons&CONT_X; p.c=s->buttons&CONT_A;
        p.start=s->buttons&CONT_START;
        p.x=s->buttons&CONT_B;
        p.mode=s->ltrig>=128 && s->rtrig>=128;
    }
}

void platform_video_init(){
    for(unsigned i=0;i<512;i++){unsigned r=(i>>6)*255/7,g=((i>>3)&7)*255/7,b=(i&7)*255/7;colorLut[i]=((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
    texture=pvr_mem_malloc(sizeof(pixels)); if(!texture)throw std::runtime_error("VRAM allocation failed");
    pvr_poly_cxt_t c; pvr_poly_cxt_txr(&c,PVR_LIST_OP_POLY,PVR_TXRFMT_RGB565|PVR_TXRFMT_NONTWIDDLED,512,256,texture,PVR_FILTER_NONE);
    pvr_poly_compile(&header,&c);
    dc_renderer_init();
    pc_profile_start();
}
void platform_video_shutdown(){dc_renderer_shutdown();pvr_mem_free(texture);}
void platform_video_present(const Framebuffer &fb,int w,int h){
    auto b=static_cast<const uint8_t*>(fb.getRawPointer());
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        auto p=b+y*Framebuffer::PITCH+x*3;
        pixels[y*512+x]=colorLut[(p[2]<<6)|(p[1]<<3)|p[0]];
    }
    if(sor::cheats::hintVisible())sor::cheats::drawHint(nullptr,[](void *,int x,int y,unsigned r,unsigned g,unsigned b){
        pixels[y*512+x]=colorLut[(r<<6)|(g<<3)|b];
    });
    pvr_wait_ready(); pvr_txr_load(pixels,texture,sizeof(pixels));
    pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);pvr_prim(&header,sizeof(header));
    pvr_vertex_t v{};v.z=1;v.argb=0xffffffff;
    const float xs[]={0,640,0,640},ys[]={0,0,480,480};
    for(int i=0;i<4;i++){v.flags=i==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;v.x=xs[i];v.y=ys[i];v.u=(i&1)?w/512.f:0;v.v=(i&2)?h/256.f:0;pvr_prim(&v,sizeof(v));}
    pvr_list_finish();pvr_scene_finish();

}
void platform_cheat_menu_present(const Framebuffer &fb){platform_video_present(fb,320,224);}
uint64_t platform_time_us(){return timer_us_gettime64();}
PlatformMemoryStats platform_memory_stats(){auto m=mallinfo();return {uint32_t(m.uordblks),uint32_t(pvr_mem_available())};}
static uint32_t lastSynthUs=0,lastPresentUs=0,slowFrames=0;
void platform_frame_parts(uint32_t synth,uint32_t present){lastSynthUs=synth;lastPresentUs=present;}
void platform_observe_frame(uint32_t frame,const sor_memory &memory,const Framebuffer &){
    // Log every interval that spans more than one VBlank, including menus.
    static uint32_t lastVblanks=0;
    {
        pvr_stats_t now{};pvr_get_stats(&now);
        uint32_t missed=now.vbl_count-lastVblanks;
        static uint64_t lastUs=0;auto us=timer_us_gettime64();
        if(lastVblanks && missed>1 && slowFrames<400){
            slowFrames++;
            uint32_t elapsed=uint32_t(us-lastUs);
            sor_log("SLOW frame=%lu vblanks=%lu interval_us=%lu synth_us=%lu present_us=%lu other_us=%ld mode=%02x%02x\n",
                (unsigned long)frame,(unsigned long)missed,(unsigned long)elapsed,(unsigned long)lastSynthUs,(unsigned long)lastPresentUs,
                long(elapsed)-long(lastSynthUs)-long(lastPresentUs),memory.ram[0xff00],memory.ram[0xff01]);
        }
        lastVblanks=now.vbl_count;lastUs=us;
    }
    static uint64_t previous=0,sum=0,worst=0;
    static uint32_t histogram[256]{},samples=0;
    static bool wasPlaying=false,reported=false;
    bool finished=replay_finished() && !reported;
    static pvr_stats_t startStats{};
    auto now=timer_us_gettime64();
    bool playing=memory.ram[0xff00]==0 && memory.ram[0xff01]==0x16;
    if(playing && !wasPlaying){
        pvr_get_stats(&startStats);sum=worst=samples=0;
        memset(histogram,0,sizeof(histogram));
    }
    if(playing && wasPlaying){
        uint64_t elapsed=now-previous;sum+=elapsed;if(elapsed>worst)worst=elapsed;
        histogram[elapsed/500<256?elapsed/500:255]++;samples++;
        if(samples%600==0 || finished){
            auto percentile=[&](unsigned n){uint32_t total=0;for(unsigned i=0;i<256;i++){total+=histogram[i];if(total*100>=samples*n)return (i+1)*500;}return 128000u;};
            pvr_stats_t stats{};pvr_get_stats(&stats);
            sor_log("FRAME_STATS n=%lu mean_us=%llu p50_us_le=%u p95_us_le=%u p99_us_le=%u worst_us=%llu vblanks=%lu flips=%lu\n",(unsigned long)samples,(unsigned long long)(sum/samples),percentile(50),percentile(95),percentile(99),(unsigned long long)worst,(unsigned long)(stats.vbl_count-startStats.vbl_count),(unsigned long)(stats.frame_count-startStats.frame_count));
        }
    }
    previous=now;wasPlaying=playing;
    pc_profile_phase(playing && (!SOR_PC_PROFILE_LAST || (samples>=SOR_PC_PROFILE_FIRST && samples<=SOR_PC_PROFILE_LAST)));
    // Interactive runs: drain diagnostics regularly so a session can be watched
    // live. Replays keep them deferred until their measured window ends.
    if(!replay_active() && samples && samples%600==0)sor_flush_log();
    // Interactive runs: every 600 emulated frames (10 s), in any mode, report
    // refresh and drain the log. Comparing these lines' arrival with the wall
    // clock separates a slow host emulator from a slow guest.
    if(!replay_active() && frame && frame%600==0){
        static pvr_stats_t last{};pvr_stats_t now{};pvr_get_stats(&now);
        sor_log("HEARTBEAT frame=%lu vblanks=%lu flips=%lu enhanced=%d mode=%02x%02x\n",(unsigned long)frame,
            (unsigned long)(now.vbl_count-last.vbl_count),(unsigned long)(now.frame_count-last.frame_count),
            int(sor::cheats::menu.settings().enhancedGraphics),memory.ram[0xff00],memory.ram[0xff01]);
        last=now;sor_flush_log();
    }
    // Drain first: a full buffer would otherwise drop the completion marker
    // that tools/bench-flycast.sh waits for, and the reports after it.
    if(finished){reported=true;sor_flush_log();sor_log("BENCHMARK replay complete; subsequent serial drain is outside the measured window\n");platform_audio_report();pc_profile_report();sor_flush_log();}
}

extern "C" {
extern const unsigned char sor_embedded_rom[] __attribute__((weak));
extern const unsigned char sor_embedded_rom_end[] __attribute__((weak));
extern const unsigned char sor_embedded_art[] __attribute__((weak));
extern const unsigned char sor_embedded_art_end[] __attribute__((weak));
}
const uint8_t *platform_embedded_rom(size_t &size){
    if(!sor_embedded_rom||!sor_embedded_rom_end){size=0;return nullptr;}
    size=uintptr_t(sor_embedded_rom_end)-uintptr_t(sor_embedded_rom);return sor_embedded_rom;
}
const uint8_t *platform_embedded_art(size_t &size){
    if(!sor_embedded_art||!sor_embedded_art_end){size=0;return nullptr;}
    size=uintptr_t(sor_embedded_art_end)-uintptr_t(sor_embedded_art);return sor_embedded_art;
}
