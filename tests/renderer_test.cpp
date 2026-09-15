#include "VDPRenderer.hpp"
#define VDPRenderer ReferenceRenderer
#include "reference-renderer.hpp"
#undef VDPRenderer
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
int main(){
 std::mt19937 random(0x534f5231);
 for(int trial=0;trial<96;trial++){
  VDPState a{},b{};a.reset();a.regs_[0]=4;a.regs_[1]=0x40;
  a.regs_[2]=0x30;a.regs_[3]=0x2c;a.regs_[4]=7;a.regs_[5]=0x50;
  a.regs_[7]=random()&63;a.regs_[12]=(trial&1)?1:0;
  a.regs_[13]=0x3f;a.regs_[16]=(trial%3==0)?0x11:0;
  a.regs_[11]=(trial%4)|((trial&4)?4:0);
  a.regs_[17]=random()&0x9f;a.regs_[18]=random()&0x9f;
  for(auto &v:a.vram_)v=random();for(auto &v:a.cram_)v=random()&0xeee;
  for(auto &v:a.vsram_)v=random();for(auto &v:a.sat_)v=random();
  b=a;VDPTile ta(a),tb(b);Framebuffer fa,fb;VDPRenderer ra(a,ta,fa);ReferenceRenderer rb(b,tb,fb);
  ra.renderFrame();rb.renderFrame();
  assert(std::memcmp(fa.getRawPointer(),fb.getRawPointer(),Framebuffer::SIZE)==0);
  assert(a.status_==b.status_);
 }
 puts("renderer: 96 deterministic random VDP scenes match upstream pixels and status");
}
