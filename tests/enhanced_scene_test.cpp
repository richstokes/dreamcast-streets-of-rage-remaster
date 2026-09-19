// Enhanced rendering: an object recorded by the sprite probe and present in the
// art catalog is drawn once with its art in its SAT slot, its hardware pieces
// are left out, other sprites stay as cells, and a probe build that no longer
// matches VRAM's sprite table falls back to the original pieces.
#include "vdp_scene.hpp"
#include "sprite_probe.hpp"
#include "art_catalog.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
namespace {
void put16(std::vector<uint8_t> &v,unsigned x){v.push_back(uint8_t(x));v.push_back(uint8_t(x>>8));}
void put32(std::vector<uint8_t> &v,uint32_t x){put16(v,x&0xFFFF);put16(v,x>>16);}
// SAT record: y, size (cells w/h), link, attr, x (all biased by 128).
void record(VDPState &s,uint8_t *ram,int index,int y,int size,int link,uint16_t attr,int x){
    const uint8_t r[8]={uint8_t(y>>8),uint8_t(y),uint8_t(size),uint8_t(link),uint8_t(attr>>8),uint8_t(attr),uint8_t(x>>8),uint8_t(x)};
    std::memcpy(s.vram_+s.satBase()+index*8,r,8);std::memcpy(ram+0xDA00+index*8,r,8);
    std::memcpy(s.sat_+index*8,r,4);
}
}
int main(){
    VDPState state;state.reset();
    state.regs_[1]=0x40;state.regs_[2]=0x30;state.regs_[4]=5;state.regs_[5]=0x78;state.regs_[12]=0x81;state.regs_[16]=0x11;
    for(int i=0;i<16;i++)state.cram_[i]=uint16_t(i*0x111&0xEEE);
    for(int i=0;i<32;i++)state.vram_[32+i]=0x11;           // tile 1: colour 1 everywhere
    std::vector<uint8_t> ram(65536,0);
    // Record 0: the object's piece (2x2 cells) at screen 40,50; record 1: an
    // unowned 1x1 sprite at 100,60, high priority; record 1 ends the list.
    record(state,ram.data(),0,128+50,0x05,1,0x0001,128+40);
    record(state,ram.data(),1,128+60,0x00,0,0x8001,128+100);
    auto &probe=sor::sprite_probe();
    probe.beginBuild();
    probe.beginObject(0xB800,1,0x054206,false,128+48,128+66,0,0xFFDA00);
    probe.endObject(0xFFDA08,ram.data());
    // One art frame for mapping $054206 palette 0: 4x4 art pixels, anchor 2,4.
    std::vector<uint8_t> pak(std::begin("SORART01"),std::end("SORART01")-1);
    put32(pak,1);put32(pak,1);put16(pak,8);put16(pak,8);
    for(int i=0;i<64;i++)put16(pak,i<8*4&&i%8<4?0xFC00:0);  // top-left 4x4 red
    put32(pak,0x054206);put16(pak,0);put16(pak,0);put16(pak,0);put16(pak,0);put16(pak,4);put16(pak,4);put16(pak,2);put16(pak,4);
    sor::ArtCatalog art;assert(art.load(pak.data(),pak.size()));
    assert(art.find(0x054206,0)&&!art.find(0x054206,1)&&!art.find(0x054207,0));

    auto scene=std::make_unique<sor::VdpScene>();
    VDPTile tile(state);Framebuffer fb;VDPRenderer renderer(state,tile,fb);
    scene->enhanced=true;scene->art=&art;
    assert(scene->build(state,renderer));
    assert(scene->artCount==1);
    const auto &d=scene->artDraws[0];
    assert(d.x==48&&d.y==66&&d.order==0&&d.layer==0&&!d.flip);
    assert(scene->spriteTileCount==1);                     // the unowned sprite only
    const auto &t=scene->spriteTiles[0];
    assert(t.x==100&&t.y==60&&t.tile==1&&t.layer==1&&t.order==1);
    std::vector<uint16_t> out(640*448);
    sor::raster_enhanced(*scene,state,out.data(),640);
    assert(out[(66*2-4)*640+48*2-2]==0xFC00);              // art's top-left at anchor - (2,4)
    assert(out[(66*2-4)*640+48*2+2]!=0xFC00);              // art is 4 pixels wide
    assert(out[(60*2)*640+100*2]==scene->colors[1]);       // unowned sprite cell

    // VRAM's table no longer matches the build: the object's pieces come back.
    state.vram_[state.satBase()+7]^=1;
    assert(!probe.displayed(state));
    assert(scene->build(state,renderer));
    assert(scene->artCount==0&&scene->spriteTileCount==5);
    puts("Enhanced scene: art replaces a probed object in its SAT slot; other sprites stay cells; stale builds fall back");
}
