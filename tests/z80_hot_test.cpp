#define Z80_DISABLE_DEBUG
#define Z80_DISABLE_BREAKPOINT
#define Z80_DISABLE_NESTCHECK
#define Z80_NO_FUNCTIONAL
#include "sor_z80.hpp"
#include "z80_hot.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
static unsigned char read(void *p,unsigned short a){return static_cast<uint8_t*>(p)[a&8191];}
static void write(void *p,unsigned short a,unsigned char v){static_cast<uint8_t*>(p)[a&8191]=v;}
static unsigned char in(void*,unsigned short){return 255;}
static void out(void*,unsigned short,unsigned char){}
int main(){
 std::array<uint8_t,8192> ram{};
 suzukiplan::Z80 reference(read,write,in,out,ram.data()),fast(read,write,in,out,ram.data());
 auto initial=reference.reg;
 auto check=[&](int clocks){
   fast.reg=reference.reg;
   int expected=reference.execute(clocks),actual=soundZ80Execute(fast,ram.data(),clocks);
   assert(expected==actual);
   assert(!memcmp(&reference.reg,&fast.reg,sizeof(reference.reg)));
 };
 // Exhaust every CP input, including undocumented flags and signed overflow.
 ram[0]=0xfe;
 for(unsigned a=0;a<256;a++)for(unsigned b=0;b<256;b++){
   reference.reg=initial;reference.reg.pair.A=a;reference.reg.R=0xff;ram[1]=b;check(1);
 }
 // Every delay count and sample deadline, including the B=0 wrap to 255.
 ram[0]=0x10;ram[1]=0xfe;ram[2]=0x76;
 for(unsigned b=0;b<256;b++)for(int deadline=1;deadline<=150;deadline++){
   reference.reg=initial;reference.reg.pair.B=b;reference.reg.R=0xfe;check(deadline);
 }
 // Synthetic polling program with different thresholds from the game driver.
 const uint8_t poll[]={0x7e,0xfe,0x20,0xda,0,0,0xfe,0x40,0xd2,0,0,0x76};
 std::copy(std::begin(poll),std::end(poll),ram.begin());
 for(unsigned value=0;value<256;value++)for(int deadline=1;deadline<120;deadline++){
   reference.reg=initial;reference.reg.pair.H=0x10;ram[0x1000]=value;check(deadline);
 }
 // Repeated short calls carry partial loop phase and refresh state forward.
 reference.reg=initial;reference.reg.pair.H=0x10;ram[0x1000]=0;
 for(unsigned n=0;n<1000;n++)check(1+n%67);
 // Verified idle-loop acceleration at every entry phase and deadline.
 ram[0x32]=0x7e;ram[0x33]=0xfe;ram[0x34]=0x81;ram[0x35]=0xda;ram[0x36]=0x32;ram[0x37]=0;
 for(unsigned value=0;value<0x81;value++)for(unsigned pc:{0x32u,0x33u,0x35u})for(int deadline=1;deadline<=150;deadline++){
   reference.reg=initial;reference.reg.PC=pc;reference.reg.pair.H=0x1f;reference.reg.pair.L=0xff;
   // Entry mid-loop requires the preceding LD/CP state to be consistent.
   reference.reg.pair.A=value;ram[0x1fff]=value;
   if(pc==0x35){reference.reg.PC=0x33;reference.execute(1);}
   fast.reg=reference.reg;int expected=reference.execute(deadline),actual=soundZ80Idle(fast,ram.data(),deadline);
   assert(expected==actual && !memcmp(&reference.reg,&fast.reg,sizeof(reference.reg)));
 }
 // Pending interrupts and wait states must retain upstream behavior.
 reference.reg=initial;reference.reg.interrupt=0x80;check(20);
 // RAM-boundary fetches and MMIO operands must go through the interpreter.
 reference.reg=initial;reference.reg.PC=8191;ram[8191]=0xfe;check(1);
 reference.reg=initial;reference.reg.pair.H=0x40;check(1);
 reference.reg=initial;reference.wtc.read=fast.wtc.read=2;check(67);
 puts("Z80 hot paths: exhaustive CP, delay counts/deadlines, polling, refresh and fallback state match");
}
