#include "vdp_scene.hpp"
#include <memory>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <array>
#include <cstring>
static uint32_t seed=0x501dc;
static uint32_t random32(){seed=seed*1664525+1013904223;return seed;}
int main(){
 auto scene=std::make_unique<sor::VdpScene>();
 auto uploaded=std::make_unique<std::array<uint16_t,2*512*256>>();
 int priorTop[2]{256,256},priorBottom[2]{};
 for(int n=0;n<160;n++){
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
    if(n>=96){
        // Crowded scanlines, zero-X masking, cyclic/out-of-range links, and
        // tile spans past VRAM exercise the frame-level sprite evaluator.
        for(int i=0;i<80;i++){
            int a=state.satBase()+i*8;
            state.sat_[i*8]=0;state.sat_[i*8+1]=150;
            state.sat_[i*8+2]=(n%4==0)?15:0;
            int x=(n%4==1 && i==4)?0:128+i%12;
            state.vram_[a+6]=x>>8;state.vram_[a+7]=x;
            if(n%4==2){state.vram_[a+4]=0xff;state.vram_[a+5]=0xff;}
        }
        if(n%4==3)state.sat_[9*8+3]=n&1?127:3;
        if(n%8==0)state.regs_[1]=0; // Clear previously occupied texture rows.
    }
    VDPState reference=state;VDPTile tile(state),refTile(reference);Framebuffer fb,refFB;
    VDPRenderer renderer(state,tile,fb),refRenderer(reference,refTile,refFB);
    if(!scene->buildCached(state))return 2;
    // Model the partial VRAM upload across changing scenes. It must equal a
    // full texture upload, including rows vacated or hidden by display disable.
    for(int p=0;p<2;p++){
        int top=std::min(priorTop[p],scene->spriteTop[p]);
        int bottom=std::max(priorBottom[p],scene->spriteBottom[p]);
        if(bottom>top)std::memcpy(uploaded->data()+p*512*256+top*512,scene->sprites[p]+top*512,(bottom-top)*1024);
        priorTop[p]=scene->spriteTop[p];priorBottom[p]=scene->spriteBottom[p];
    }
    if(std::memcmp(uploaded->data(),scene->sprites,sizeof(scene->sprites))){puts("Partial sprite upload left stale pixels");return 1;}
    refRenderer.renderFrame();uint16_t out[320*240];sor::raster_scene(*scene,state,out);
    for(int y=0;y<state.activeHeight();y++)for(int x=0;x<state.activeWidth();x++){
        auto p=refFB.pixels_+y*Framebuffer::PITCH+x*3;
        if(out[y*320+x]!=sor::VdpScene::rgb1555(p[2],p[1],p[0])){printf("Mismatch scene %d at %d,%d\n",n,x,y);return 1;}
    }
    // A status-port read may clear collision/overflow between identical frames.
    // Reusing drawing commands must still reproduce those hardware side effects.
    state.status_&=~0x60;reference.status_&=~0x60;
    if(!scene->buildCached(state)||!scene->reused)return 3;
    refRenderer.renderFrame();
    if(state.status_!=reference.status_ || state.vCounter_!=reference.vCounter_){puts("VDP status mismatch");return 1;}
 }
 puts("160 scenes: pixels, window/scroll/flip/priority, sprite limits/collisions, partial uploads and VCounter match");
}
