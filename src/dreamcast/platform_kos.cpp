#include <kos.h>
#include <malloc.h>
#include <stdexcept>
#include "platform.hpp"
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
}
void platform_video_shutdown(){pvr_mem_free(texture);}
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
void platform_observe_frame(uint32_t,const sor_memory &,const Framebuffer &){}
