#include <kos.h>
#include <malloc.h>
#include <stdexcept>
#include "platform.hpp"
void dc_renderer_init();
void dc_renderer_shutdown();
bool dc_render_vdp(VDPState &,VDPRenderer &);
static bool useGpu=true,previousToggle=false;
bool platform_render_vdp(VDPState &s,VDPRenderer &r){return useGpu && dc_render_vdp(s,r);}
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
        if(i==0){bool toggle=s->buttons&CONT_B;if(toggle&&!previousToggle){useGpu=!useGpu;printf("Renderer: %s\n",useGpu?"PowerVR":"software comparison");}previousToggle=toggle;}
        auto &p=*out[i]; p.connected=true;
        p.up=s->buttons&CONT_DPAD_UP; p.down=s->buttons&CONT_DPAD_DOWN;
        p.left=s->buttons&CONT_DPAD_LEFT; p.right=s->buttons&CONT_DPAD_RIGHT;
        p.a=s->buttons&CONT_Y; p.b=s->buttons&CONT_X; p.c=s->buttons&CONT_A;
        p.start=s->buttons&CONT_START;
    }
}

void platform_video_init(){
    for(unsigned i=0;i<512;i++){unsigned r=(i>>6)*255/7,g=((i>>3)&7)*255/7,b=(i&7)*255/7;colorLut[i]=((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
    texture=pvr_mem_malloc(sizeof(pixels)); if(!texture)throw std::runtime_error("VRAM allocation failed");
    pvr_poly_cxt_t c; pvr_poly_cxt_txr(&c,PVR_LIST_OP_POLY,PVR_TXRFMT_RGB565|PVR_TXRFMT_NONTWIDDLED,512,256,texture,PVR_FILTER_NONE);
    pvr_poly_compile(&header,&c);
    dc_renderer_init();
}
void platform_video_shutdown(){dc_renderer_shutdown();pvr_mem_free(texture);}
void platform_video_present(const Framebuffer &fb,int w,int h){
    auto b=static_cast<const uint8_t*>(fb.getRawPointer());
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        auto p=b+y*Framebuffer::PITCH+x*3;
        pixels[y*512+x]=colorLut[(p[2]<<6)|(p[1]<<3)|p[0]];
    }
    pvr_wait_ready(); pvr_txr_load(pixels,texture,sizeof(pixels));
    pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);pvr_prim(&header,sizeof(header));
    pvr_vertex_t v{};v.z=1;v.argb=0xffffffff;
    const float xs[]={0,640,0,640},ys[]={0,0,480,480};
    for(int i=0;i<4;i++){v.flags=i==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;v.x=xs[i];v.y=ys[i];v.u=(i&1)?w/512.f:0;v.v=(i&2)?h/256.f:0;pvr_prim(&v,sizeof(v));}
    pvr_list_finish();pvr_scene_finish();

}
uint64_t platform_time_us(){return timer_us_gettime64();}
PlatformMemoryStats platform_memory_stats(){auto m=mallinfo();return {uint32_t(m.uordblks),uint32_t(pvr_mem_available())};}
void platform_observe_frame(uint32_t,const sor_memory &memory,const Framebuffer &){
    static uint64_t previous=0,sum=0,worst=0;
    static uint32_t histogram[256]{},samples=0;
    static bool wasPlaying=false;
    auto now=timer_us_gettime64();
    bool playing=memory.ram[0xff00]==0 && memory.ram[0xff01]==0x16;
    if(playing && wasPlaying){
        uint64_t elapsed=now-previous;sum+=elapsed;if(elapsed>worst)worst=elapsed;
        histogram[elapsed/500<256?elapsed/500:255]++;samples++;
        if(samples%600==0){
            auto percentile=[&](unsigned n){uint32_t total=0;for(unsigned i=0;i<256;i++){total+=histogram[i];if(total*100>=samples*n)return (i+1)*500;}return 128000u;};
            printf("FRAME_STATS n=%lu mean_us=%llu p50_us_le=%u p95_us_le=%u p99_us_le=%u worst_us=%llu\n",(unsigned long)samples,(unsigned long long)(sum/samples),percentile(50),percentile(95),percentile(99),(unsigned long long)worst);
        }
    }
    previous=now;wasPlaying=playing;
}

extern "C" {
extern const unsigned char sor_embedded_rom[] __attribute__((weak));
extern const unsigned char sor_embedded_rom_end[] __attribute__((weak));
}
const uint8_t *platform_embedded_rom(size_t &size){
    if(!sor_embedded_rom||!sor_embedded_rom_end){size=0;return nullptr;}
    size=uintptr_t(sor_embedded_rom_end)-uintptr_t(sor_embedded_rom);return sor_embedded_rom;
}
