#include "audio_core.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <span>
int main(){
 NativeAudio disabled(false);int16_t silence[1780];disabled.setReset(true);disabled.setBusRequest(false);disabled.writeYM(0,0x28);disabled.writePSG(0x90);assert(disabled.renderFrame(silence)==0&&disabled.readYM(0)==0);
 NativeAudio a,b;int16_t x[1780],y[1780];unsigned total=0;
 for(auto *s:{&a,&b}){s->writePSG(0x84);s->writePSG(0x08);s->writePSG(0x90);}
 int lo=32767,hi=-32768;
 for(int frame=0;frame<120;frame++){
  unsigned n=a.renderFrame(x);assert(n==b.renderFrame(y)&&n<=890);total+=n;
  assert(!memcmp(x,y,n*4));for(unsigned i=0;i<n*2;i++){lo=std::min(lo,int(x[i]));hi=std::max(hi,int(x[i]));}
 }
 assert(total==uint64_t(896040)*120/1008 && lo<0 && hi>0);
 NativeAudio melody;
 auto reg=[&](unsigned a,unsigned v){melody.writeYM(0,a);melody.writeYM(1,v);};
 for(unsigned op:{0u,4u,8u,12u}){reg(0x30+op,1);reg(0x40+op,0);reg(0x50+op,31);reg(0x60+op,0);reg(0x70+op,0);reg(0x80+op,15);}
 reg(0xb0,7);reg(0xb4,0xc0);reg(0xa4,0x22);reg(0xa0,0x69);reg(0x28,0xf0);
 unsigned melodicFrames=melody.renderFrame(x);lo=32767;hi=-32768;
 for(int sample:std::span(x,melodicFrames*2)){lo=std::min(lo,sample);hi=std::max(hi,sample);}assert(lo < -1000 && hi > 1000);
 NativeAudio dac;dac.writeYM(2,0xb6);dac.writeYM(3,0xc0);dac.writeYM(0,0x2b);dac.writeYM(1,0x80);dac.writeYM(0,0x2a);dac.writeYM(1,0xff);
 auto n=dac.renderFrame(x);assert(x[(n-1)*2]>1000 && x[(n-1)*2+1]>1000 && dac.dacWrites==1);
 NativeAudio z;const uint8_t rom[]={0x42};z.setROM(rom,sizeof(rom));
 // Synthetic Z80 program: read banked ROM, store to local RAM, halt.
 const uint8_t program[]={0x3a,0x00,0x80,0x32,0x00,0x01,0x76};
 for(unsigned i=0;i<sizeof(program);i++)z.writeRAMFor68K(i,program[i]);
 z.setReset(true);z.setReset(false);z.setBusRequest(true);z.renderFrame(x);assert(z.ram[0x100]==0);
 z.setBusRequest(false);z.renderFrame(x);assert(z.ram[0x100]==0x42&&z.z80Faults==0);
 // One byte beyond the supplied ROM must return open bus, never host memory.
 z.writeRAMFor68K(1,1);z.setReset(true);z.setReset(false);z.renderFrame(x);
 assert(z.ram[0x100]==0xff&&z.z80Faults==1);
 puts("Audio: deterministic stereo, rational cadence, PSG/FM signals, DAC panning and banked Z80 bus pass");
}
