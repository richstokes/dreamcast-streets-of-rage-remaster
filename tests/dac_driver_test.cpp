#define Z80_DISABLE_DEBUG
#define Z80_DISABLE_BREAKPOINT
#define Z80_DISABLE_NESTCHECK
#define Z80_NO_FUNCTIONAL
#include "sor_z80.hpp"
#include "dac_driver.hpp"
#include <array>
#include <vector>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <cstring>
struct Bus {
 std::array<uint8_t,8192> ram{};const std::vector<uint8_t>*rom;unsigned bank=0,address=0;
 std::vector<uint8_t> pcm;
 static uint8_t read(void *ctx,uint16_t a){auto &s=*static_cast<Bus*>(ctx);if(a<0x4000)return s.ram[a&8191];if(a<0x6000)return 0;if(a>=0x8000){size_t p=(s.bank<<15)+(a&32767);assert(p<s.rom->size());return (*s.rom)[p];}return 255;}
 static void write(void *ctx,uint16_t a,uint8_t v){auto &s=*static_cast<Bus*>(ctx);if(a<0x4000)s.ram[a&8191]=v;else if(a==0x4000)s.address=v;else if(a==0x4001 && s.address==0x2a)s.pcm.push_back(v);else if((a&0xff00)==0x6000)s.bank=((s.bank>>1)|((v&1)<<8))&511;}
 static uint8_t in(void*,uint16_t){return 255;}static void out(void*,uint16_t,uint8_t){}
};
int main(int argc,char **argv){
 assert(argc==3);std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)),{});
 std::array<uint8_t,8192> initial;std::ifstream(argv[2],std::ios::binary).read((char*)initial.data(),initial.size());
 assert(NativeDacDriver::recognizes(initial.data()));unsigned checked=0;
 for(unsigned quantum:{67u,59733u})for(unsigned command=0x81;command<=0x91;command++)for(unsigned stop:{0u,5000u,1u}){
  if(stop==1 && command!=0x81)continue;
  Bus ref;ref.ram=initial;ref.rom=&rom;suzukiplan::Z80 cpu(Bus::read,Bus::write,Bus::in,Bus::out,&ref);
  for(int i=0;cpu.reg.PC!=0x32;i++){assert(i<10000);cpu.execute(1);}
  ref.ram[0x1fff]=command;bool empty=false;
  for(int i=0;cpu.reg.PC!=0xd9;i++){assert(i<10000);cpu.execute(1);if(cpu.reg.PC==0x2f){empty=true;break;}}
  if(empty)continue;
  if(stop==1){ // Synthetic zero-count repeat and delay wrap, one payload byte.
   cpu.reg.pair.D=0x1e;cpu.reg.pair.E=0xf0;cpu.reg.pair.B=0;cpu.reg.pair.C=1;
   ref.ram[0x1ef0]=0;ref.ram[0x1e]=0;ref.ram[cpu.reg.IY+4]=0;
  }
  Bus fast=ref;NativeDacDriver native(fast.ram.data(),&fast,Bus::read,Bus::write);
  auto &r=cpu.reg;native.start((r.pair.D<<8)|r.pair.E,(r.pair.B<<8)|r.pair.C,r.IY,r.back.C,r.SP);
  uint64_t rt=0,nt=0,target=0;bool ended=false,interrupted=false;
  while(native.active()||!ended){
   target+=quantum+(target%5==0);
   if(stop>1 && target>=stop&&!interrupted){ref.ram[0x1fff]=fast.ram[0x1fff]=0x81;interrupted=true;}
   while(rt<target&&!ended){rt+=cpu.execute(1);ended=cpu.reg.PC==0x2f;}
   if(nt<target&&native.active())nt+=native.advance(int(target-nt));
   if(rt!=nt || ref.pcm!=fast.pcm || ref.ram!=fast.ram || ended==native.active()){
    std::fprintf(stderr,"command %x stop %u target %llu times %llu/%llu pc %x samples %zu/%zu end %d/%d\n",command,stop,(unsigned long long)target,(unsigned long long)rt,(unsigned long long)nt,cpu.reg.PC,ref.pcm.size(),fast.pcm.size(),ended,!native.active());
    for(unsigned i=0;i<8192;i++)if(ref.ram[i]!=fast.ram[i])std::fprintf(stderr,"ram %x %x/%x\n",i,ref.ram[i],fast.ram[i]);
    return 1;
   }
   assert(target<100000000);
  }
  checked++;std::printf("DAC %02x stop=%u samples=%zu clocks=%llu match\n",command,stop,ref.pcm.size(),(unsigned long long)rt);
 }
 assert(checked==66);
}
