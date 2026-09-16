#include "ymfm_opn.h"
#include <cstdio>
#include <cstdint>
// Synthetic register traffic, independent of ROM assets. Compile unchanged
// against pinned and staged ymfm to compare every sample across output paths.
int main(){
 ymfm::ymfm_interface interface;ymfm::ym2612 chip(interface);chip.reset();
 uint32_t seed=0x736f7231;
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 auto reg=[&](unsigned bank,unsigned address,unsigned value){chip.write(bank*2,address);chip.write(bank*2+1,value);};
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
   for(unsigned n=0;n<32;n++){
     ymfm::ym2612::output_data output;chip.generate(&output);
     // Serialize without relying on host word size or endianness.
     for(auto value:output.data)for(unsigned byte=0;byte<4;byte++)
       if(std::putchar((uint32_t(value)>>(byte*8))&255)==EOF)return 1;
   }
 }
 // A long quiescent interval followed by key-on exercises deferred phase,
 // feedback history and cache flush, beyond the engine's 4096-sample refresh.
 chip.reset();
 for(unsigned n=0;n<32768;n++){
   if(n==8191 || n==16383){
     std::vector<uint8_t> saved;ymfm::ymfm_saved_state save(saved,true);chip.save_restore(save);
     chip.reset();ymfm::ymfm_saved_state restore(saved,false);chip.save_restore(restore);
   }
   if(n==24576){
     for(unsigned op=0;op<4;op++){
       reg(0,0x30+op*4,1);reg(0,0x40+op*4,0);reg(0,0x50+op*4,31);
       reg(0,0x80+op*4,15);
     }
     reg(0,0xa4,0x22);reg(0,0xa0,0x69);reg(0,0xb0,7);reg(0,0xb4,0xc0);reg(0,0x28,0xf0);
   }
   ymfm::ym2612::output_data output;chip.generate(&output);
   for(auto value:output.data)for(unsigned byte=0;byte<4;byte++)
     if(std::putchar((uint32_t(value)>>(byte*8))&255)==EOF)return 1;
 }
 return std::fflush(stdout)!=0;
}
