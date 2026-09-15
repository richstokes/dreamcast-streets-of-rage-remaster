#include <kos.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include "vdp_scene.hpp"
namespace {
std::unique_ptr<sor::VdpScene> scene;
pvr_ptr_t tiles=nullptr,spriteTexture[2]{};
pvr_poly_hdr_t tileHeaders[8192],spriteHeaders[2];
uint8_t previousTiles[65536]{},valid[2048]{};
uint16_t previousColors[64]{};
bool opaque[2048]{};
uint32_t frames=0;
void header(pvr_poly_hdr_t &h,pvr_ptr_t texture,int w,int hgt,bool linear){
    pvr_poly_cxt_t c;
    pvr_poly_cxt_txr(&c,PVR_LIST_PT_POLY,PVR_TXRFMT_ARGB1555|(linear?PVR_TXRFMT_NONTWIDDLED:0),w,hgt,texture,PVR_FILTER_NONE);
    c.gen.culling=PVR_CULLING_NONE;
    c.depth.comparison=PVR_DEPTHCMP_GEQUAL;
    pvr_poly_compile(&h,&c);
}
void quad(const pvr_poly_hdr_t &h,float x,float y,float w,float hgt,float z,float u0,float v0,float u1,float v1){
    struct alignas(32) Packet {pvr_poly_hdr_t header;pvr_vertex_t vertices[4];};
    Packet packet{};packet.header=h;
    const float xx[]={x,x+w,x,x+w},yy[]={y,y,y+hgt,y+hgt};
    for(int i=0;i<4;i++){auto &v=packet.vertices[i];v.z=z;v.argb=0xffffffff;v.flags=i==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;v.x=xx[i];v.y=yy[i];v.u=(i&1)?u1:u0;v.v=(i&2)?v1:v0;}
    pvr_prim(&packet,sizeof(packet));
}
}
void dc_renderer_init(){
    scene=std::make_unique<sor::VdpScene>();
    tiles=pvr_mem_malloc(8192*128);
    for(int p=0;p<2;p++)spriteTexture[p]=pvr_mem_malloc(512*256*2);
    if(!tiles||!spriteTexture[0]||!spriteTexture[1])throw std::runtime_error("PowerVR texture budget exhausted");
    for(int i=0;i<8192;i++)header(tileHeaders[i],static_cast<uint8_t*>(tiles)+i*128,8,8,false);
    for(int p=0;p<2;p++)header(spriteHeaders[p],spriteTexture[p],512,256,true);
    printf("PowerVR tile cache: 1048576 bytes; sprite layers: 524288 bytes\n");
}
void dc_renderer_shutdown(){
    if(tiles)pvr_mem_free(tiles);
    for(auto p:spriteTexture)if(p)pvr_mem_free(p);
    scene.reset();
}
bool dc_render_vdp(VDPState &state,VDPRenderer &renderer){
    const auto begin=timer_us_gettime64();
    if(!scene->buildCached(state,renderer)||scene->count>6000)return false;
    const bool same=scene->reused;
    const auto compiled=timer_us_gettime64();
    pvr_wait_ready();
    for(int p=0;p<4;p++)if(std::memcmp(previousColors+p*16,scene->colors+p*16,32)){
        for(auto &v:valid)v&=~(1<<p);
        std::memcpy(previousColors+p*16,scene->colors+p*16,32);
    }
    for(int t=0;t<2048;t++)if(std::memcmp(previousTiles+t*32,state.vram_+t*32,32)){
        valid[t]=0;std::memcpy(previousTiles+t*32,state.vram_+t*32,32);
        opaque[t]=false;for(int j=0;j<32;j++)opaque[t]|=state.vram_[t*32+j]!=0;
    }
    unsigned uploads=0;
    alignas(32) uint16_t decoded[64];
    for(size_t i=0;i<scene->count;i++){
        const auto &q=scene->quads[i];int t=q.tile,p=q.palette;
        if(!opaque[t] || (valid[t]&(1<<p)))continue;
        for(int j=0;j<64;j++){unsigned b=state.vram_[t*32+j/2],c=(j&1)?b&15:b>>4;decoded[j]=c?scene->colors[p*16+c]:0;}
        pvr_txr_load_ex(decoded,static_cast<uint8_t*>(tiles)+(p*2048+t)*128,8,8,PVR_TXRLOAD_16BPP);
        valid[t]|=1<<p;uploads++;
    }
    if(!same || !frames)for(int p=0;p<2;p++)pvr_txr_load(scene->sprites[p],spriteTexture[p],512*256*2);
    const auto uploaded=timer_us_gettime64();
    auto bg=scene->background;pvr_set_bg_color(((bg>>10)&31)/31.f,((bg>>5)&31)/31.f,(bg&31)/31.f);
    pvr_scene_begin();pvr_list_begin(PVR_LIST_PT_POLY);
    float sx=640.f/scene->width,sy=480.f/scene->height;
    for(size_t i=0;i<scene->count;i++){
        const auto &q=scene->quads[i];if(!opaque[q.tile])continue;
        quad(tileHeaders[q.palette*2048+q.tile],q.x*sx,q.y*sy,q.w*sx,q.h*sy,q.depth,q.u0/8.f,q.v0/8.f,q.u1/8.f,q.v1/8.f);
    }
    for(int p=0;p<2;p++)quad(spriteHeaders[p],0,0,640,480,p?6:3,0,0,scene->width/512.f,scene->height/256.f);
    pvr_list_finish();pvr_scene_finish();
    if(frames++%600==0)printf("GPU quads=%u tiles_uploaded=%u compile_us=%llu wait_upload_us=%llu submit_us=%llu\n",unsigned(scene->count),uploads,(unsigned long long)(compiled-begin),(unsigned long long)(uploaded-compiled),(unsigned long long)(timer_us_gettime64()-uploaded));
    return true;
}
