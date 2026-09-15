#include "vdp_scene.hpp"
#include <memory>
#include <cstdio>
#include <cstdint>
static uint32_t seed=0x501dc;
static uint32_t random32(){seed=seed*1664525+1013904223;return seed;}
int main(){
 auto scene=std::make_unique<sor::VdpScene>();
 for(int n=0;n<96;n++){
    VDPState state;state.reset();
    for(auto &v:state.vram_)v=random32()>>24;
    for(auto &c:state.cram_)c=random32()&0xeee;
    state.regs_[0]=n&1?4:0;state.regs_[1]=0x40;
    state.regs_[2]=0x30;state.regs_[3]=0x2c;state.regs_[4]=5;state.regs_[5]=0x78;
    state.regs_[7]=random32()&63;state.regs_[11]=(n%3==0?3:n%3==1?2:0);
    state.regs_[12]=n&1;state.regs_[13]=0x3f;
    state.regs_[16]=(n%3==0?0:n%3==1?0x11:0x33);
    state.regs_[17]=random32()&0x9f;state.regs_[18]=random32()&0x9f;
    state.vsram_[0]=random32();state.vsram_[1]=random32();
    for(int i=0;i<80;i++){
        unsigned base=i*8,y=128+(random32()%240);
        state.sat_[base]=y>>8;state.sat_[base+1]=y;
        state.sat_[base+2]=random32()&15;state.sat_[base+3]=i==79?0:i+1;
        unsigned a=state.satBase()+base,x=random32()%512;
        state.vram_[a+6]=x>>8;state.vram_[a+7]=x;
    }
    VDPState reference=state;VDPTile tile(state),refTile(reference);Framebuffer fb,refFB;
    VDPRenderer renderer(state,tile,fb),refRenderer(reference,refTile,refFB);
    if(!scene->buildCached(state,renderer))return 2;
    refRenderer.renderFrame();uint16_t out[320*240];sor::raster_scene(*scene,state,out);
    for(int y=0;y<state.activeHeight();y++)for(int x=0;x<state.activeWidth();x++){
        auto p=refFB.pixels_+y*Framebuffer::PITCH+x*3;
        if(out[y*320+x]!=sor::VdpScene::rgb1555(p[2],p[1],p[0])){printf("Mismatch scene %d at %d,%d\n",n,x,y);return 1;}
    }
    // A status-port read may clear collision/overflow between identical frames.
    // Reusing drawing commands must still reproduce those hardware side effects.
    state.status_&=~0x60;reference.status_&=~0x60;
    if(!scene->buildCached(state,renderer)||!scene->reused)return 3;
    refRenderer.renderFrame();
    if(state.status_!=reference.status_ || state.vCounter_!=reference.vCounter_){puts("VDP status mismatch");return 1;}
 }
 puts("96 scenes: pixels, window/scroll/flip/priority, sprite limits/collisions and VCounter match");
}
