#include "ymfm_opn.h"
#include <cstdio>
#include <cstdint>
#include <vector>
// Synthetic register traffic, independent of ROM assets. Compile unchanged
// against pinned and staged ymfm to compare every sample across output paths.
int main(){
 ymfm::ymfm_interface interface;ymfm::ym2612 chip(interface);chip.reset();
 uint32_t seed=0x736f7231;
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 auto reg=[&](unsigned bank,unsigned address,unsigned value){chip.write(bank*2,address);chip.write(bank*2+1,value);};
 // Span splits use their own generator so register traffic is identical in
 // both builds. The staged build renders through the channel-major span path.
 uint32_t splitSeed=0x7370616e;
 [[maybe_unused]] auto split=[&](){splitSeed^=splitSeed<<13;splitSeed^=splitSeed>>17;splitSeed^=splitSeed<<5;return splitSeed;};
 std::vector<ymfm::ym2612::output_data> buffer;
 auto generate=[&](unsigned n){
   buffer.resize(n);
#ifdef SOR_SPAN
   for(unsigned at=0;at<n;){unsigned k=std::min<unsigned>(n-at,1+split()%(split()&1?n:64));chip.sor_generate_span(&buffer[at],k);at+=k;}
#else
   for(unsigned i=0;i<n;i++)chip.generate(&buffer[i]);
#endif
   // Serialize without relying on host word size or endianness.
   for(auto &output:buffer)for(auto value:output.data)for(unsigned byte=0;byte<4;byte++)
     if(std::putchar((uint32_t(value)>>(byte*8))&255)==EOF)return false;
   return true;
 };
 for(unsigned block=0;block<2048;block++){
   unsigned channel=random()%3,bank=random()%2,op=(random()%4)*4;
   for(unsigned address:{0x30u,0x40u,0x50u,0x60u,0x70u,0x80u,0x90u})reg(bank,address+op+channel,random());
   reg(bank,0xa4+channel,random());reg(bank,0xa0+channel,random());
   reg(bank,0xb0+channel,random());reg(bank,0xb4+channel,random());
   reg(0,0x22,random());reg(0,0x28,(random()&0xf0)|(bank?4:0)|channel);
   reg(0,0x2b,random());reg(0,0x2a,random());
   if(block%127==0){
     std::vector<uint8_t> saved;ymfm::ymfm_saved_state save(saved,true);chip.save_restore(save);
     chip.reset();ymfm::ymfm_saved_state restore(saved,false);chip.save_restore(restore);
   }
   if(!generate(32))return 1;
 }
 // A long quiescent interval followed by key-on exercises deferred phase,
 // feedback history and cache flush, beyond the engine's 4096-sample refresh.
 chip.reset();
 unsigned done=0;
 for(unsigned event:{8191u,16383u,24576u,32768u}){
   if(!generate(event-done))return 1;
   done=event;
   if(event==8191 || event==16383){
     std::vector<uint8_t> saved;ymfm::ymfm_saved_state save(saved,true);chip.save_restore(save);
     chip.reset();ymfm::ymfm_saved_state restore(saved,false);chip.save_restore(restore);
   }
   if(event==24576){
     for(unsigned op=0;op<4;op++){
       reg(0,0x30+op*4,1);reg(0,0x40+op*4,0);reg(0,0x50+op*4,31);
       reg(0,0x80+op*4,15);
     }
     reg(0,0xa4,0x22);reg(0,0xa0,0x69);reg(0,0xb0,7);reg(0,0xb4,0xc0);reg(0,0x28,0xf0);
   }
 }
 // Long intervals: LFO AM/PM, all algorithms and pans, DAC toggles, SSG-EG,
 // key-off tails and periodic prepares inside a single constant-register span.
 chip.reset();
 for(unsigned block=0;block<96;block++){
   unsigned bank=random()%2,channel=random()%3;
   for(unsigned op=0;op<16;op+=4){
     reg(bank,0x30+op+channel,random());reg(bank,0x40+op+channel,random()&0x3f);
     reg(bank,0x50+op+channel,random());reg(bank,0x60+op+channel,random()|((block&1)<<7));
     reg(bank,0x70+op+channel,random()&31);reg(bank,0x80+op+channel,random());
     reg(bank,0x90+op+channel,block%5==0?8|(random()&7):0);
   }
   reg(bank,0xa4+channel,random()&0x3f);reg(bank,0xa0+channel,random());
   reg(bank,0xb0+channel,random());reg(bank,0xb4+channel,random());
   reg(0,0x22,block%3?8|(random()&7):0);reg(0,0x2b,block%4==1?0x80:0);reg(0,0x2a,random());
   reg(0,0x28,(block%7==6?0:0xf0)|(bank?4:0)|channel);
   if(!generate(1+random()%(block%8==0?9000:1500)))return 1;
 }
 return std::fflush(stdout)!=0;
}
